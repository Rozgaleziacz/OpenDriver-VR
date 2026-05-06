#pragma once

#include <opendriver/core/plugin_interface.h>
#include <opendriver/core/event_bus.h>
#include <opendriver/core/device_registry.h>

#include <atomic>
#include <mutex>
#include <thread>
#include <deque>
#include <vector>
#include <string>
#include <cstdint>
#include <cmath>
#include <chrono>

// Platform socket includes
#ifdef _WIN32
#  include <winsock2.h>
#  include <ws2tcpip.h>
#  pragma comment(lib, "ws2_32.lib")
   using socket_t = SOCKET;
#  define INVALID_SOCK INVALID_SOCKET
#  define SOCK_ERR     SOCKET_ERROR
#  define sock_close   closesocket
#else
#  include <sys/socket.h>
#  include <netinet/in.h>
#  include <netinet/tcp.h>
#  include <arpa/inet.h>
#  include <unistd.h>
#  include <fcntl.h>
#  include <errno.h>
   using socket_t = int;
#  define INVALID_SOCK (-1)
#  define SOCK_ERR     (-1)
#  define sock_close   close
#endif

namespace android_hmd {

// ============================================================================
// TRACKING DATA (z telefonu przez UDP)
// ============================================================================

struct TrackingPose {
    // Pozycja (metry)
    double x = 0.0, y = 1.6, z = 0.0;

    // Rotacja (kwaternion)
    double qw = 1.0, qx = 0.0, qy = 0.0, qz = 0.0;

    // Prędkości liniowe (m/s)
    double linVelX = 0.0, linVelY = 0.0, linVelZ = 0.0;

    // Prędkości kątowe (rad/s)
    double angVelX = 0.0, angVelY = 0.0, angVelZ = 0.0;
};

// ============================================================================
// UDP TRACKING RECEIVER
// ============================================================================

class UdpTrackingReceiver {
public:
    UdpTrackingReceiver() = default;
    ~UdpTrackingReceiver() { Stop(); }

    bool Start(uint16_t port);
    void Stop();

    // Pobiera najnowszą pozę (thread-safe)
    // Zwraca false jeśli od ostatniego wywołania nic nowego nie przyszło
    bool GetLatestPose(TrackingPose& out);

    // Statystyki
    uint64_t GetPacketCount() const { return m_packet_count.load(); }
    uint64_t GetDroppedCount() const { return m_dropped_count.load(); }
    float    GetHz() const { return m_hz.load(); }

private:
    void ReceiveLoop();

    socket_t              m_sock = INVALID_SOCK;
    std::thread           m_thread;
    std::atomic<bool>     m_running{false};

    mutable std::mutex    m_pose_mutex;
    TrackingPose          m_latest_pose;
    bool                  m_has_new_pose = false;

    std::atomic<uint64_t> m_packet_count{0};
    std::atomic<uint64_t> m_dropped_count{0};
    std::atomic<float>    m_hz{0.0f};
};

// ============================================================================
// TCP VIDEO SENDER
// ============================================================================

// Binarny nagłówek ramki wysyłanej do Androida
// Wielokrotność 4 bajtów, little-endian
struct FrameHeader {
    uint32_t magic       = 0x48564452; // "HVDR"
    uint32_t frame_size  = 0;          // rozmiar danych po tym nagłówku
    uint64_t frame_num   = 0;
    uint64_t pts         = 0;          // prezentacja timestamp (ns)
    uint8_t  is_keyframe = 0;
    uint8_t  _pad[7]     = {};
};
static_assert(sizeof(FrameHeader) == 32, "FrameHeader must be 32 bytes");

// Nagłówek konfiguracyjny (pierwsza wiadomość po połączeniu)
struct ConfigHeader {
    uint32_t magic        = 0x43464748; // "CFG\x48"
    uint32_t width        = 0;
    uint32_t height       = 0;
    float    refresh_rate = 90.0f;
    uint8_t  _pad[16]     = {};
};
static_assert(sizeof(ConfigHeader) == 32, "ConfigHeader must be 32 bytes");

class TcpVideoSender {
public:
    TcpVideoSender() = default;
    ~TcpVideoSender() { Stop(); }

    // Uruchom serwer TCP na podanym porcie
    bool Start(uint16_t port, uint32_t width, uint32_t height, float refresh_rate);
    void Stop();

    // Wysyła ramkę do wszystkich podłączonych klientów
    // Wywołuj z wątku eventowego (bridge callback) — wewnętrznie jest kolejka
    void EnqueueFrame(std::vector<uint8_t> nal_data, uint64_t frame_num,
                      uint64_t pts, bool is_keyframe);

    bool HasClients() const { return m_client_count.load() > 0; }
    int  GetClientCount() const { return m_client_count.load(); }
    uint64_t GetSentBytes() const { return m_sent_bytes.load(); }
    float    GetSentFPS() const { return m_sent_fps.load(); }

private:
    void AcceptLoop();
    void SendLoop();
    void SendToClient(socket_t client_sock, const ConfigHeader& cfg);
    bool SendAll(socket_t sock, const void* data, size_t len);

    struct PendingFrame {
        std::vector<uint8_t> nal;
        uint64_t frame_num;
        uint64_t pts;
        bool     is_keyframe;
    };

    socket_t          m_listen_sock = INVALID_SOCK;
    std::thread       m_accept_thread;
    std::thread       m_send_thread;
    std::atomic<bool> m_running{false};

    mutable std::mutex          m_clients_mutex;
    std::vector<socket_t>       m_clients;
    std::atomic<int>            m_client_count{0};

    mutable std::mutex          m_queue_mutex;
    std::condition_variable     m_queue_cv;
    std::deque<PendingFrame>    m_queue;
    static constexpr size_t     MAX_QUEUE = 8;  // drop oldest if overflowed

    ConfigHeader              m_config_header;

    std::atomic<uint64_t>     m_sent_bytes{0};
    std::atomic<float>        m_sent_fps{0.0f};
};

// ============================================================================
// POSE PROCESSOR — kalkulacja prędkości + EMA + deadzone + axis offsets
// ============================================================================

class PoseProcessor {
public:
    struct Config {
        // Stabilny krok czasu (NIE używamy czasu pakietu!)
        double sensor_hz     = 90.0;

        // Pitch offset osi telefonu → SteamVR (stopnie)
        double pitch_offset_deg = 0.0;

        // EMA smoothing (0 = brak, 1 = zamrożenie)
        double ema_alpha_pos = 0.05;
        double ema_alpha_rot = 0.08;
        double ema_alpha_vel = 0.15;

        // Deadzone — poniżej tego kąta (rad) rotacja ignorowana
        double rot_deadzone_rad = 0.003;
        // Deadzone pozycji (m)
        double pos_deadzone_m   = 0.001;
    };

    explicit PoseProcessor(const Config& cfg) : m_cfg(cfg) {}

    // Wejście: surowa poza z UDP
    // Wyjście: poza gotowa do UpdatePose()
    TrackingPose Process(const TrackingPose& raw);

    void SetConfig(const Config& cfg) { m_cfg = cfg; }
    const Config& GetConfig() const { return m_cfg; }

private:
    Config       m_cfg;
    TrackingPose m_prev_raw;
    TrackingPose m_smoothed;
    bool         m_initialized = false;

    // Helpers
    static double Clamp(double v, double lo, double hi) {
        return v < lo ? lo : (v > hi ? hi : v);
    }
    static double QuatDotAbs(double aw, double ax, double ay, double az,
                              double bw, double bx, double by, double bz) {
        return std::abs(aw*bw + ax*bx + ay*by + az*bz);
    }
    void ApplyPitchOffset(TrackingPose& pose) const;
};

// ============================================================================
// UI PROVIDER — statystyki w Dashboard (Qt)
// ============================================================================

class HmdUIProvider : public opendriver::core::IUIProvider {
public:
    struct Stats {
        float   tracking_hz   = 0.0f;
        float   video_fps     = 0.0f;
        uint64_t udp_packets  = 0;
        uint64_t udp_dropped  = 0;
        int     tcp_clients   = 0;
        uint64_t sent_bytes   = 0;
        bool    is_active     = false;
        std::string status;
    };

    QWidget* CreateSettingsWidget(QWidget* parent) override;
    void RefreshUI() override;

    void UpdateStats(const Stats& s);

private:
    // Wskaźniki do widżetów Qt (ustawiane w CreateSettingsWidget)
    // Deklarujemy jako void* żeby nie ciągnąć Qt headers w tym pliku
    void* m_label_tracking_hz = nullptr;
    void* m_label_video_fps   = nullptr;
    void* m_label_clients     = nullptr;
    void* m_label_packets     = nullptr;
    void* m_label_dropped     = nullptr;
    void* m_label_sent_mb     = nullptr;
    void* m_label_status      = nullptr;

    mutable std::mutex m_stats_mutex;
    Stats m_stats;
};

// ============================================================================
// MAIN PLUGIN CLASS
// ============================================================================

class AndroidHmdPlugin : public opendriver::core::IPlugin {
public:
    AndroidHmdPlugin() = default;
    ~AndroidHmdPlugin() override = default;

    // ── Metadata ──────────────────────────────────────────────────────
    const char* GetName()        const override { return "android_hmd"; }
    const char* GetVersion()     const override { return "1.0.0"; }
    const char* GetDescription() const override {
        return "Phone-as-HMD via UDP tracking + TCP H264 video streaming";
    }
    const char* GetAuthor()      const override { return "OpenDriver-VR"; }

    // ── Lifecycle ─────────────────────────────────────────────────────
    bool OnInitialize(opendriver::core::IPluginContext* context) override;
    void OnShutdown() override;

    // ── Per-frame ─────────────────────────────────────────────────────
    void OnTick(float delta_time) override;

    // ── Events ────────────────────────────────────────────────────────
    void OnEvent(const opendriver::core::Event& event) override;

    // ── Status ────────────────────────────────────────────────────────
    bool IsActive() const override { return m_active.load(); }
    std::string GetStatus() const override;

    // ── UI ────────────────────────────────────────────────────────────
    opendriver::core::IUIProvider* GetUIProvider() override { return &m_ui; }

    // ── Hot reload ────────────────────────────────────────────────────
    void* ExportState() override;
    void  ImportState(void* state) override;

private:
    void LoadConfig();
    void RegisterHmdDevice();

    opendriver::core::IPluginContext* m_context = nullptr;
    std::atomic<bool>                 m_active{false};

    // Sub-systems
    UdpTrackingReceiver m_udp;
    TcpVideoSender      m_tcp;
    PoseProcessor*      m_processor = nullptr;
    HmdUIProvider       m_ui;

    // Config
    uint16_t    m_udp_port      = 6969;
    uint16_t    m_tcp_port      = 6970;
    uint32_t    m_display_w     = 1920;
    uint32_t    m_display_h     = 1080;
    float       m_refresh_rate  = 90.0f;
    float       m_fov           = 100.0f;
    std::string m_device_id     = "android_hmd_001";

    PoseProcessor::Config m_pose_cfg;
};

} // namespace android_hmd
