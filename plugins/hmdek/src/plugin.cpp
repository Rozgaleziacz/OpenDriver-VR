#include "android_hmd_plugin.h"

#include <nlohmann/json.hpp>
#include <opendriver/core/config_manager.h>
#include <opendriver/core/bridge.h>
#include <opendriver/core/platform.h>
#include <sstream>
#include <iomanip>
#include <cstring>
#include <any>

namespace android_hmd {

namespace {

std::vector<uint8_t> ExtractSequenceHeaderAnnexB(const std::vector<uint8_t>& nal) {
    std::vector<uint8_t> header;

    for (size_t i = 0; i + 4 < nal.size(); ++i) {
        if (nal[i] != 0x00 || nal[i + 1] != 0x00) {
            continue;
        }

        size_t start = 0;
        if (nal[i + 2] == 0x01) {
            start = i + 3;
        } else if (nal[i + 2] == 0x00 && nal[i + 3] == 0x01) {
            start = i + 4;
        } else {
            continue;
        }

        size_t next = nal.size();
        for (size_t j = start; j + 4 < nal.size(); ++j) {
            if (nal[j] == 0x00 && nal[j + 1] == 0x00 &&
                (nal[j + 2] == 0x01 ||
                 (nal[j + 2] == 0x00 && nal[j + 3] == 0x01))) {
                next = j;
                break;
            }
        }

        if (start >= nal.size()) {
            continue;
        }

        const uint8_t nal_type = nal[start] & 0x1F;
        if (nal_type == 7 || nal_type == 8) {
            header.insert(header.end(), nal.begin() + i, nal.begin() + next);
        }
    }

    return header;
}

bool ContainsIdr(const std::vector<uint8_t>& nal) {
    for (size_t i = 0; i + 4 < nal.size(); ++i) {
        if (nal[i] != 0x00 || nal[i + 1] != 0x00) {
            continue;
        }

        size_t start = 0;
        if (nal[i + 2] == 0x01) {
            start = i + 3;
        } else if (nal[i + 2] == 0x00 && nal[i + 3] == 0x01) {
            start = i + 4;
        }

        if (start < nal.size() && (nal[start] & 0x1F) == 5) {
            return true;
        }
    }

    return false;
}

std::string PluginConfigPath(const std::string& key) {
    return "plugins.android_hmd." + key;
}

} // namespace

// ============================================================================
// AndroidHmdPlugin — Lifecycle
// ============================================================================

bool AndroidHmdPlugin::OnInitialize(opendriver::core::IPluginContext* context) {
    m_context = context;
    m_last_video_sender_ip.clear();
    m_video_frames_seen = 0;

    m_context->LogInfo("[android_hmd] Initializing...");

    m_ui.SetSaveCallback([this](const HmdUIProvider::SettingsState& settings) {
        return SaveSettings(settings);
    });
    m_ui.SetShowEncoderSettingsCallback([this]() {
        if (m_context) {
            m_context->RequestShowEncoderSettings();
        }
    });

    // ── 1. Wczytaj config ─────────────────────────────────────────────
    LoadConfig();

    // ── 2. Zbuduj PoseProcessor z wczytaną konfiguracją ───────────────
    m_processor = new PoseProcessor(m_pose_cfg);

    // ── 3. Zarejestruj urządzenie HMD w SteamVR ───────────────────────
    RegisterHmdDevice();

    // ── 4. Subskrybuj VIDEO_FRAME z Bridge (H264 ze SteamVR) ──────────
    m_context->GetEventBus().Subscribe(opendriver::core::EventType::VIDEO_FRAME, this);

    // ── 5. Uruchom UDP receiver ────────────────────────────────────────
    if (!m_udp.Start(m_udp_port)) {
        m_context->LogError("[android_hmd] Failed to bind UDP socket");
        return false;
    }
    {
        std::ostringstream ss;
        ss << "[android_hmd] UDP tracking listener on port " << m_udp_port;
        m_context->LogInfo(ss.str().c_str());
    }

    // ── 6. Uruchom TCP video sender ────────────────────────────────────
    if (!m_tcp.Start(m_tcp_port, m_display_w, m_display_h, m_refresh_rate)) {
        m_context->LogError("[android_hmd] Failed to start TCP video server");
        m_udp.Stop();
        return false;
    }
    m_tcp.SetLogger([this](const std::string& message) {
        if (m_context) {
            m_context->LogInfo(message.c_str());
        }
    });
    {
        std::ostringstream ss;
        ss << "[android_hmd] TCP video sender ready for port " << m_tcp_port;
        m_context->LogInfo(ss.str().c_str());
    }

    m_active = true;
    RefreshUiSettings();
    m_context->LogInfo("[android_hmd] Initialized OK");
    return true;
}

void AndroidHmdPlugin::OnShutdown() {
    m_active = false;

    m_context->GetEventBus().Unsubscribe(opendriver::core::EventType::VIDEO_FRAME, this);

    m_udp.Stop();
    m_tcp.Stop();

    m_context->UnregisterDevice(m_device_id.c_str());

    delete m_processor;
    m_processor = nullptr;

    m_context->LogInfo("[android_hmd] Shutdown complete");
}

// ============================================================================
// AndroidHmdPlugin — LoadConfig
// ============================================================================

void AndroidHmdPlugin::LoadConfig() {
    m_device_id    = GetConfigString("device_id", "android_hmd_001");
    m_udp_port     = static_cast<uint16_t>(GetConfigInt("udp_port", 6969));
    m_tcp_port     = static_cast<uint16_t>(GetConfigInt("tcp_port", 6970));
    m_display_w    = static_cast<uint32_t>(GetConfigInt("display_width", 1920));
    m_display_h    = static_cast<uint32_t>(GetConfigInt("display_height", 1080));
    m_refresh_rate = GetConfigFloat("refresh_rate", 90.0f);
    m_fov          = GetConfigFloat("fov", 100.0f);

    m_pose_cfg.sensor_hz        = GetConfigFloat("sensor_hz", 90.0f);
    m_pose_cfg.pitch_offset_deg = GetConfigFloat("pitch_offset_deg", -90.0f);
    m_pose_cfg.ema_alpha_pos    = GetConfigFloat("ema_alpha_pos", 0.35f);
    m_pose_cfg.ema_alpha_rot    = GetConfigFloat("ema_alpha_rot", 0.45f);
    m_pose_cfg.ema_alpha_vel    = GetConfigFloat("ema_alpha_vel", 0.35f);
    m_pose_cfg.rot_deadzone_rad = GetConfigFloat("rot_deadzone_rad", 0.0015f);
    m_pose_cfg.pos_deadzone_m   = GetConfigFloat("pos_deadzone_m", 0.001f);
}

// ============================================================================
// AndroidHmdPlugin — RegisterHmdDevice
// ============================================================================

void AndroidHmdPlugin::RegisterHmdDevice() {
    opendriver::core::Device hmd;
    hmd.id             = m_device_id;
    hmd.type           = opendriver::core::DeviceType::HMD;
    hmd.name           = "Android Phone HMD";
    hmd.manufacturer   = "OpenDriver-VR";
    hmd.serial_number  = "ANDROID-HMD-001";
    hmd.owner_plugin   = GetName();
    hmd.connected      = true;

    hmd.display.width        = m_display_w;
    hmd.display.height       = m_display_h;
    hmd.display.refresh_rate = m_refresh_rate;
    hmd.display.fov_left     = m_fov;
    hmd.display.fov_right    = m_fov;

    hmd.tracking.has_gyro    = true;
    hmd.tracking.has_accel   = true;
    hmd.tracking.has_compass = false;
    hmd.tracking.update_rate = (uint32_t)m_pose_cfg.sensor_hz;

    m_context->RegisterDevice(hmd);

    {
        std::ostringstream ss;
        ss << "[android_hmd] Registered HMD: " << m_device_id
           << " (" << m_display_w << "x" << m_display_h
           << " @ " << m_refresh_rate << "Hz, FOV=" << m_fov << ")";
        m_context->LogInfo(ss.str().c_str());
    }
}

// ============================================================================
// AndroidHmdPlugin — OnTick (~90x/s)
//
// Główna pętla: pobierz pozę z UDP, przetwórz, wyślij do SteamVR.
// Wąska pętla — zero blokujących operacji.
// ============================================================================

void AndroidHmdPlugin::OnTick(float /*delta_time*/) {
    if (!m_active || !m_processor) return;

    TrackingPose raw_pose;
    if (!m_udp.GetLatestPose(raw_pose)) {
        // Brak nowego pakietu — UpdatePose z poprzednią pozą (SteamVR tego wymaga)
        // Processor pamięta ostatnią wygładzoną pozę — wyślij ją ponownie z zerowymi velocity
        // (wystarczy; SteamVR sam ekstrapoluje na podstawie poprzednich prędkości)
        return;
    }

    const std::string sender_ip = m_udp.GetLastSenderIp();
    if (!sender_ip.empty()) {
        if (sender_ip != m_last_video_sender_ip) {
            m_last_video_sender_ip = sender_ip;
            std::ostringstream ss;
            ss << "[android_hmd] Tracking source IP detected: " << sender_ip
               << " (video target updated)";
            m_context->LogInfo(ss.str().c_str());
        }
        m_tcp.UpdateRemoteHost(sender_ip);
    }

    // Oblicz prędkości, wygładź, zastosuj offsets
    TrackingPose final_pose = m_processor->Process(raw_pose);

    // Prześlij do SteamVR przez Bridge
    m_context->UpdatePose(
        m_device_id.c_str(),
        final_pose.x,   final_pose.y,   final_pose.z,
        final_pose.qw,  final_pose.qx,  final_pose.qy,  final_pose.qz,
        final_pose.linVelX, final_pose.linVelY, final_pose.linVelZ,
        final_pose.angVelX, final_pose.angVelY, final_pose.angVelZ
    );

    // Odśwież statystyki UI (nie co klatkę — za wolno dla Qt; co ~100ms wystarczy)
    static int tick_counter = 0;
    if (++tick_counter >= 9) {  // ~90Hz / 9 = ~10x/s
        tick_counter = 0;
        HmdUIProvider::Stats stats;
        stats.tracking_hz  = m_udp.GetHz();
        stats.video_fps    = m_tcp.GetSentFPS();
        stats.udp_packets  = m_udp.GetPacketCount();
        stats.udp_dropped  = m_udp.GetDroppedCount();
        stats.tcp_clients  = m_tcp.GetClientCount();
        stats.sent_bytes   = m_tcp.GetSentBytes();
        stats.is_active    = m_active.load();
        stats.status       = GetStatus();
        m_ui.UpdateStats(stats);
    }
}

HmdUIProvider::SettingsState AndroidHmdPlugin::GetCurrentSettings() const {
    HmdUIProvider::SettingsState settings;
    settings.width = static_cast<int>(m_display_w);
    settings.height = static_cast<int>(m_display_h);
    settings.refresh_rate = m_refresh_rate;
    settings.fov = m_fov;
    settings.sensor_hz = static_cast<float>(m_pose_cfg.sensor_hz);
    settings.pitch_offset_deg = static_cast<float>(m_pose_cfg.pitch_offset_deg);
    settings.ema_alpha_rot = static_cast<float>(m_pose_cfg.ema_alpha_rot);
    return settings;
}

std::string AndroidHmdPlugin::SaveSettings(const HmdUIProvider::SettingsState& settings) {
    auto& cfg = m_context->GetConfig();

    SetConfigInt("display_width", settings.width);
    SetConfigInt("display_height", settings.height);
    SetConfigFloat("refresh_rate", settings.refresh_rate);
    SetConfigFloat("fov", settings.fov);
    SetConfigFloat("sensor_hz", settings.sensor_hz);
    SetConfigFloat("pitch_offset_deg", settings.pitch_offset_deg);
    SetConfigFloat("ema_alpha_rot", settings.ema_alpha_rot);

    if (!cfg.Save()) {
        return "Save failed: config.json could not be written.";
    }

    m_display_w = static_cast<uint32_t>(settings.width);
    m_display_h = static_cast<uint32_t>(settings.height);
    m_refresh_rate = settings.refresh_rate;
    m_fov = settings.fov;
    m_pose_cfg.sensor_hz = settings.sensor_hz;
    m_pose_cfg.pitch_offset_deg = settings.pitch_offset_deg;
    m_pose_cfg.ema_alpha_rot = settings.ema_alpha_rot;
    if (m_processor) {
        m_processor->SetConfig(m_pose_cfg);
    }

    RefreshUiSettings();
    return "Saved. Tracking changes are live; resolution/FOV/refresh require SteamVR restart.";
}

void AndroidHmdPlugin::RefreshUiSettings() {
    m_ui.UpdateSettings(GetCurrentSettings());
}

int AndroidHmdPlugin::GetConfigInt(const std::string& key, int default_value) {
    auto& cfg = m_context->GetConfig();
    const std::string plugin_path = PluginConfigPath(key);
    const int plugin_value = cfg.GetInt(plugin_path, default_value);
    if (plugin_value != default_value) {
        return plugin_value;
    }
    return cfg.GetInt(key, default_value);
}

float AndroidHmdPlugin::GetConfigFloat(const std::string& key, float default_value) {
    auto& cfg = m_context->GetConfig();
    const std::string plugin_path = PluginConfigPath(key);
    const float plugin_value = cfg.GetFloat(plugin_path, default_value);
    if (plugin_value != default_value) {
        return plugin_value;
    }
    return cfg.GetFloat(key, default_value);
}

std::string AndroidHmdPlugin::GetConfigString(const std::string& key, const std::string& default_value) {
    auto& cfg = m_context->GetConfig();
    const std::string plugin_path = PluginConfigPath(key);
    const std::string plugin_value = cfg.GetString(plugin_path, default_value);
    if (plugin_value != default_value) {
        return plugin_value;
    }
    return cfg.GetString(key, default_value);
}

void AndroidHmdPlugin::SetConfigInt(const std::string& key, int value) {
    auto& cfg = m_context->GetConfig();
    cfg.SetInt(PluginConfigPath(key), value);
    cfg.SetInt(key, value);
}

void AndroidHmdPlugin::SetConfigFloat(const std::string& key, float value) {
    auto& cfg = m_context->GetConfig();
    cfg.SetFloat(PluginConfigPath(key), value);
    cfg.SetFloat(key, value);
}

void AndroidHmdPlugin::SetConfigString(const std::string& key, const std::string& value) {
    auto& cfg = m_context->GetConfig();
    cfg.SetString(PluginConfigPath(key), value);
    cfg.SetString(key, value);
}

// ============================================================================
// AndroidHmdPlugin — OnEvent
//
// Obsługa VIDEO_FRAME z Bridge (H264 NAL units ze SteamVR compositor).
// Wywoływane z wątku Bridge, nie z OnTick — thread-safe przez TcpVideoSender.
// ============================================================================

void AndroidHmdPlugin::OnEvent(const opendriver::core::Event& event) {
    if (event.type != opendriver::core::EventType::VIDEO_FRAME) return;
    if (!m_active) return;

    // VIDEO_FRAME data is serialized as std::vector<uint8_t> to avoid
    // std::any_cast RTTI mismatches across DLL boundaries.
    // See VideoFrameData::Serialize() / Deserialize() in bridge.h.
    const auto* raw = std::any_cast<std::vector<uint8_t>>(&event.data);
    if (!raw || raw->size() < 16) {
        return;
    }

    opendriver::core::VideoFrameData frame_data;
    if (!opendriver::core::VideoFrameData::Deserialize(*raw, frame_data)) {
        return;
    }

    if (frame_data.nal_data.empty()) {
        return;
    }

    const auto sequence_header = ExtractSequenceHeaderAnnexB(frame_data.nal_data);
    if (!sequence_header.empty()) {
        m_tcp.UpdateSequenceHeader(sequence_header);
    }

    const bool is_keyframe = ContainsIdr(frame_data.nal_data);
    ++m_video_frames_seen;

    if (m_video_frames_seen == 1 || (m_video_frames_seen % 300) == 0) {
        std::ostringstream ss;
        ss << "[android_hmd] VIDEO_FRAME received #" << m_video_frames_seen
           << " size=" << frame_data.nal_data.size()
           << " keyframe=" << (is_keyframe ? "yes" : "no")
           << " clients=" << m_tcp.GetClientCount();
        m_context->LogInfo(ss.str().c_str());
    }

    m_tcp.EnqueueFrame(frame_data.nal_data, frame_data.frame_number,
                       frame_data.pts, is_keyframe);
}

// ============================================================================
// AndroidHmdPlugin — GetStatus
// ============================================================================

std::string AndroidHmdPlugin::GetStatus() const {
    std::ostringstream ss;
    ss << std::fixed << std::setprecision(1);
    ss << "Tracking: " << m_udp.GetHz()         << " Hz\n";
    ss << "Video:    " << m_tcp.GetSentFPS()     << " FPS\n";
    ss << "Clients:  " << m_tcp.GetClientCount() << "\n";
    ss << "Packets:  " << m_udp.GetPacketCount() << "\n";
    ss << "Dropped:  " << m_udp.GetDroppedCount() << "\n";
    ss << "Sent:     "
       << std::setprecision(2)
       << (m_tcp.GetSentBytes() / (1024.0 * 1024.0)) << " MB\n";
    ss << "UDP port: " << m_udp_port << "\n";
    ss << "TCP port: " << m_tcp_port << "\n";
    return ss.str();
}

// ============================================================================
// AndroidHmdPlugin — Hot Reload State Export/Import
// ============================================================================

struct PluginState {
    uint64_t udp_packet_count;
    uint64_t sent_bytes;
};

void* AndroidHmdPlugin::ExportState() {
    auto* state          = new PluginState;
    state->udp_packet_count = m_udp.GetPacketCount();
    state->sent_bytes       = m_tcp.GetSentBytes();
    return state;
}

void AndroidHmdPlugin::ImportState(void* raw_state) {
    if (!raw_state) return;
    // Stan jest informacyjny (statystyki) — nie ma co restorować
    // Sieć i urządzenie są re-inicjalizowane w OnInitialize()
    delete static_cast<PluginState*>(raw_state);
}

} // namespace android_hmd

// ============================================================================
// FACTORY FUNCTIONS — wymagane przez OpenDriver plugin loader
// ============================================================================

extern "C" {

OD_EXPORT opendriver::core::IPlugin* CreatePlugin() {
    return new android_hmd::AndroidHmdPlugin();
}

OD_EXPORT void DestroyPlugin(opendriver::core::IPlugin* plugin) {
    delete plugin;
}

} // extern "C"
