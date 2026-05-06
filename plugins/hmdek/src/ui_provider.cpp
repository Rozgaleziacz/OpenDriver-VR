#include "android_hmd_plugin.h"

#ifdef WITH_QT_UI
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QFrame>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QSignalBlocker>
#include <QStringList>
#include <QVBoxLayout>
#include <QWidget>
#endif

#include <iomanip>
#include <sstream>

namespace android_hmd {

#ifdef WITH_QT_UI
namespace {

QString ResolutionValue(const HmdUIProvider::SettingsState& settings) {
    return QString("%1x%2").arg(settings.width).arg(settings.height);
}

bool ParseResolution(const QString& text, int& width, int& height) {
    const QString normalized = text.trimmed().toLower();
    const QStringList parts = normalized.split('x');
    if (parts.size() != 2) {
        return false;
    }

    bool ok_width = false;
    bool ok_height = false;
    const int parsed_width = parts[0].trimmed().toInt(&ok_width);
    const int parsed_height = parts[1].trimmed().toInt(&ok_height);
    if (!ok_width || !ok_height || parsed_width <= 0 || parsed_height <= 0) {
        return false;
    }

    width = parsed_width;
    height = parsed_height;
    return true;
}

void EnsureResolutionOption(QComboBox* combo, const QString& value) {
    if (!combo) {
        return;
    }

    const int index = combo->findText(value);
    if (index >= 0) {
        combo->setCurrentIndex(index);
        return;
    }

    combo->addItem(value);
    combo->setCurrentIndex(combo->count() - 1);
}

} // namespace
#endif

QWidget* HmdUIProvider::CreateSettingsWidget(QWidget* parent) {
#ifdef WITH_QT_UI
    auto* root = new QWidget(parent);
    auto* layout = new QVBoxLayout(root);
    layout->setContentsMargins(8, 8, 8, 8);
    layout->setSpacing(8);

    auto* status_label = new QLabel("Offline", root);
    status_label->setStyleSheet(
        "QLabel { font-weight: bold; font-size: 13px; color: #888; padding: 4px; }");
    layout->addWidget(status_label);
    m_label_status = status_label;

    auto* line = new QFrame(root);
    line->setFrameShape(QFrame::HLine);
    line->setFrameShadow(QFrame::Sunken);
    layout->addWidget(line);

    auto* tracking_group = new QGroupBox("Tracking", root);
    auto* tracking_layout = new QVBoxLayout(tracking_group);

    auto* hz_label = new QLabel("Rate: - Hz", tracking_group);
    tracking_layout->addWidget(hz_label);
    m_label_tracking_hz = hz_label;

    auto* pkt_label = new QLabel("Packets: 0", tracking_group);
    tracking_layout->addWidget(pkt_label);
    m_label_packets = pkt_label;

    auto* drop_label = new QLabel("Dropped: 0", tracking_group);
    tracking_layout->addWidget(drop_label);
    m_label_dropped = drop_label;

    layout->addWidget(tracking_group);

    auto* video_group = new QGroupBox("Video", root);
    auto* video_layout = new QVBoxLayout(video_group);

    auto* fps_label = new QLabel("FPS: - fps", video_group);
    video_layout->addWidget(fps_label);
    m_label_video_fps = fps_label;

    auto* clients_label = new QLabel("Clients: 0", video_group);
    video_layout->addWidget(clients_label);
    m_label_clients = clients_label;

    auto* sent_label = new QLabel("Sent: 0.00 MB", video_group);
    video_layout->addWidget(sent_label);
    m_label_sent_mb = sent_label;

    layout->addWidget(video_group);

    auto* settings_group = new QGroupBox("Settings", root);
    auto* settings_layout = new QVBoxLayout(settings_group);
    auto* form = new QFormLayout();

    auto* resolution_combo = new QComboBox(settings_group);
    resolution_combo->setEditable(true);
    resolution_combo->addItems(QStringList()
        << "1280x720"
        << "1600x900"
        << "1920x1080"
        << "2160x1200"
        << "2560x1440");
    form->addRow("Resolution", resolution_combo);
    m_combo_resolution = resolution_combo;

    auto* refresh_spin = new QDoubleSpinBox(settings_group);
    refresh_spin->setRange(24.0, 144.0);
    refresh_spin->setDecimals(1);
    refresh_spin->setSingleStep(5.0);
    form->addRow("Refresh Hz", refresh_spin);
    m_spin_refresh = refresh_spin;

    auto* fov_spin = new QDoubleSpinBox(settings_group);
    fov_spin->setRange(60.0, 140.0);
    fov_spin->setDecimals(1);
    fov_spin->setSingleStep(1.0);
    form->addRow("FOV", fov_spin);
    m_spin_fov = fov_spin;

    auto* sensor_spin = new QDoubleSpinBox(settings_group);
    sensor_spin->setRange(30.0, 240.0);
    sensor_spin->setDecimals(1);
    sensor_spin->setSingleStep(5.0);
    form->addRow("Sensor Hz", sensor_spin);
    m_spin_sensor_hz = sensor_spin;

    auto* pitch_spin = new QDoubleSpinBox(settings_group);
    pitch_spin->setRange(-180.0, 180.0);
    pitch_spin->setDecimals(1);
    pitch_spin->setSingleStep(1.0);
    form->addRow("Pitch Offset", pitch_spin);
    m_spin_pitch_offset = pitch_spin;

    auto* ema_spin = new QDoubleSpinBox(settings_group);
    ema_spin->setRange(0.01, 0.99);
    ema_spin->setDecimals(2);
    ema_spin->setSingleStep(0.01);
    form->addRow("EMA Rot", ema_spin);
    m_spin_ema_rot = ema_spin;

    settings_layout->addLayout(form);

    auto* actions_layout = new QHBoxLayout();
    auto* save_button = new QPushButton("Save", settings_group);
    auto* open_encoder_button = new QPushButton("Open Encoder Settings", settings_group);
    actions_layout->addWidget(save_button);
    actions_layout->addWidget(open_encoder_button);
    actions_layout->addStretch();
    settings_layout->addLayout(actions_layout);

    auto* settings_status = new QLabel(
        "Tracking values apply live. Resolution/FOV/refresh need SteamVR restart.",
        settings_group);
    settings_status->setWordWrap(true);
    settings_layout->addWidget(settings_status);
    m_label_settings_status = settings_status;

    layout->addWidget(settings_group);
    layout->addStretch();

    QObject::connect(save_button, &QPushButton::clicked, root, [this]() {
        auto* combo = static_cast<QComboBox*>(m_combo_resolution);
        auto* refresh = static_cast<QDoubleSpinBox*>(m_spin_refresh);
        auto* fov = static_cast<QDoubleSpinBox*>(m_spin_fov);
        auto* sensor = static_cast<QDoubleSpinBox*>(m_spin_sensor_hz);
        auto* pitch = static_cast<QDoubleSpinBox*>(m_spin_pitch_offset);
        auto* ema = static_cast<QDoubleSpinBox*>(m_spin_ema_rot);
        auto* status = static_cast<QLabel*>(m_label_settings_status);

        if (!combo || !refresh || !fov || !sensor || !pitch || !ema || !status) {
            return;
        }

        int width = 0;
        int height = 0;
        if (!ParseResolution(combo->currentText(), width, height)) {
            status->setText("Invalid resolution. Use format like 1920x1080.");
            return;
        }

        SettingsState settings;
        settings.width = width;
        settings.height = height;
        settings.refresh_rate = static_cast<float>(refresh->value());
        settings.fov = static_cast<float>(fov->value());
        settings.sensor_hz = static_cast<float>(sensor->value());
        settings.pitch_offset_deg = static_cast<float>(pitch->value());
        settings.ema_alpha_rot = static_cast<float>(ema->value());

        if (m_save_callback) {
            status->setText(QString::fromStdString(m_save_callback(settings)));
        } else {
            status->setText("Save callback is not available.");
        }
    });

    QObject::connect(open_encoder_button, &QPushButton::clicked, root, [this]() {
        auto* status = static_cast<QLabel*>(m_label_settings_status);
        if (m_show_encoder_settings_callback) {
            m_show_encoder_settings_callback();
            if (status) {
                status->setText("Opening Video Encoding settings...");
            }
        } else if (status) {
            status->setText("Encoder settings action is not available.");
        }
    });

    RefreshUI();
    return root;
#else
    (void)parent;
    return nullptr;
#endif
}

void HmdUIProvider::RefreshUI() {
#ifdef WITH_QT_UI
    Stats stats;
    SettingsState settings;
    {
        std::lock_guard<std::mutex> stats_lock(m_stats_mutex);
        stats = m_stats;
    }
    {
        std::lock_guard<std::mutex> settings_lock(m_settings_mutex);
        settings = m_settings;
    }

    auto* status = static_cast<QLabel*>(m_label_status);
    auto* hz = static_cast<QLabel*>(m_label_tracking_hz);
    auto* pkt = static_cast<QLabel*>(m_label_packets);
    auto* drop = static_cast<QLabel*>(m_label_dropped);
    auto* fps = static_cast<QLabel*>(m_label_video_fps);
    auto* clients = static_cast<QLabel*>(m_label_clients);
    auto* sent = static_cast<QLabel*>(m_label_sent_mb);
    auto* resolution = static_cast<QComboBox*>(m_combo_resolution);
    auto* refresh = static_cast<QDoubleSpinBox*>(m_spin_refresh);
    auto* fov = static_cast<QDoubleSpinBox*>(m_spin_fov);
    auto* sensor = static_cast<QDoubleSpinBox*>(m_spin_sensor_hz);
    auto* pitch = static_cast<QDoubleSpinBox*>(m_spin_pitch_offset);
    auto* ema = static_cast<QDoubleSpinBox*>(m_spin_ema_rot);

    if (status) {
        if (stats.is_active) {
            status->setText(QString("Online | %1")
                .arg(QString::fromStdString(stats.status.substr(0, stats.status.find('\n')))));
            status->setStyleSheet(
                "QLabel { font-weight: bold; font-size: 13px; color: #4CAF50; padding: 4px; }");
        } else {
            status->setText("Offline");
            status->setStyleSheet(
                "QLabel { font-weight: bold; font-size: 13px; color: #F44336; padding: 4px; }");
        }
    }

    if (hz) {
        hz->setText(QString("Rate: %1 Hz").arg(stats.tracking_hz, 0, 'f', 1));
    }
    if (pkt) {
        pkt->setText(QString("Packets: %1").arg(stats.udp_packets));
    }
    if (drop) {
        drop->setText(QString("Dropped: %1").arg(stats.udp_dropped));
    }
    if (fps) {
        fps->setText(QString("FPS: %1 fps").arg(stats.video_fps, 0, 'f', 1));
    }
    if (clients) {
        clients->setText(QString("Clients: %1").arg(stats.tcp_clients));
    }
    if (sent) {
        sent->setText(QString("Sent: %1 MB").arg(stats.sent_bytes / (1024.0 * 1024.0), 0, 'f', 2));
    }

    if (resolution) {
        const QSignalBlocker blocker(*resolution);
        EnsureResolutionOption(resolution, ResolutionValue(settings));
    }
    if (refresh) {
        const QSignalBlocker blocker(*refresh);
        refresh->setValue(settings.refresh_rate);
    }
    if (fov) {
        const QSignalBlocker blocker(*fov);
        fov->setValue(settings.fov);
    }
    if (sensor) {
        const QSignalBlocker blocker(*sensor);
        sensor->setValue(settings.sensor_hz);
    }
    if (pitch) {
        const QSignalBlocker blocker(*pitch);
        pitch->setValue(settings.pitch_offset_deg);
    }
    if (ema) {
        const QSignalBlocker blocker(*ema);
        ema->setValue(settings.ema_alpha_rot);
    }
#endif
}

void HmdUIProvider::UpdateStats(const Stats& s) {
    std::lock_guard<std::mutex> lock(m_stats_mutex);
    m_stats = s;
}

void HmdUIProvider::UpdateSettings(const SettingsState& settings) {
    std::lock_guard<std::mutex> lock(m_settings_mutex);
    m_settings = settings;
}

} // namespace android_hmd
