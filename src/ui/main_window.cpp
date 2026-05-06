#include "main_window.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QTabWidget>
#include <QPushButton>
#include <QListView>
#include <QTableView>
#include <QLineEdit>
#include <QHeaderView>
#include <QSplitter>
#include <QTextEdit>
#include <QSpinBox>
#include <QComboBox>
#include <QSlider>
#include <QCheckBox>
#include <QGroupBox>
#include <opendriver/core/logger.h>
#include <opendriver/core/platform.h>
#include <QDateTime>
#include <QMessageBox>
#include <nlohmann/json.hpp>
#include <filesystem>
#include <fstream>
#include <cstdlib>
#include <algorithm>

namespace opendriver::ui {

MainWindow::MainWindow(opendriver::core::Runtime* runtime, QWidget* parent)
    : QMainWindow(parent), m_runtime(runtime) {
    
    m_deviceModel = new DeviceModel(&m_runtime->GetDeviceRegistry(), this);
    m_pluginModel = new PluginModel(m_runtime, this);
    
    setupUI();
    
    // Subskrypcja logów
    opendriver::core::Logger::GetInstance().AddListener([this](const opendriver::core::LogEntry& entry){
        // Używamy invokeMethod aby zapewnić bezpieczeństwo wątkowe (logi przychodzą z różnych wątków)
        QMetaObject::invokeMethod(this, [this, entry](){
            this->onLogReceived(entry);
        }, Qt::QueuedConnection);
    });

    m_refreshTimer = new QTimer(this);
    connect(m_refreshTimer, &QTimer::timeout, this, &MainWindow::onRefreshTimer);
    m_refreshTimer->start(500); // 2Hz wystarczy dla listy
}

MainWindow::~MainWindow() {}

void MainWindow::setupUI() {
    setWindowTitle("OpenDriver VR Dashboard");
    setWindowIcon(QIcon(":/icons/icon.png"));
    resize(1000, 750);
    setMinimumSize(800, 600);

    // ══════════════════════════════════════════════════════════════════════
    // GLOBAL DARK THEME STYLESHEET
    // ══════════════════════════════════════════════════════════════════════
    setStyleSheet(R"(
        * { font-family: 'Segoe UI', 'Inter', 'Roboto', sans-serif; }

        QMainWindow { background: #0f1117; }

        QTabWidget::pane {
            border: 1px solid #2a2d3a;
            background: #161822;
            border-radius: 8px;
        }
        QTabBar::tab {
            background: #1c1f2e;
            color: #8b8fa3;
            padding: 10px 22px;
            margin-right: 2px;
            border-top-left-radius: 8px;
            border-top-right-radius: 8px;
            font-weight: 600;
            font-size: 12px;
        }
        QTabBar::tab:selected {
            background: #161822;
            color: #e0e4f0;
            border-bottom: 2px solid #6c63ff;
        }
        QTabBar::tab:hover:!selected {
            background: #22253a;
            color: #c0c4d4;
        }

        QGroupBox {
            background: #1c1f2e;
            border: 1px solid #2a2d3a;
            border-radius: 10px;
            margin-top: 14px;
            padding: 18px 14px 14px 14px;
            font-weight: 700;
            font-size: 13px;
            color: #c8cce0;
        }
        QGroupBox::title {
            subcontrol-origin: margin;
            subcontrol-position: top left;
            padding: 4px 12px;
            background: #6c63ff;
            color: white;
            border-radius: 6px;
            font-size: 11px;
        }

        QLabel { color: #b0b4c8; font-size: 12px; }

        QPushButton {
            background: qlineargradient(x1:0, y1:0, x2:1, y2:0,
                stop:0 #6c63ff, stop:1 #8b5cf6);
            color: white;
            border: none;
            border-radius: 6px;
            padding: 8px 18px;
            font-weight: 600;
            font-size: 12px;
        }
        QPushButton:hover {
            background: qlineargradient(x1:0, y1:0, x2:1, y2:0,
                stop:0 #7c73ff, stop:1 #9b6cf6);
        }
        QPushButton:pressed { background: #5a52e0; }
        QPushButton[danger="true"] {
            background: qlineargradient(x1:0, y1:0, x2:1, y2:0,
                stop:0 #ef4444, stop:1 #f87171);
        }
        QPushButton[secondary="true"] {
            background: #2a2d3a;
            color: #b0b4c8;
        }
        QPushButton[secondary="true"]:hover { background: #363a4e; }

        QLineEdit {
            background: #1c1f2e;
            border: 1px solid #2a2d3a;
            border-radius: 6px;
            padding: 8px 12px;
            color: #e0e4f0;
            font-size: 12px;
        }
        QLineEdit:focus { border: 1px solid #6c63ff; }

        QTableView, QListView {
            background: #1c1f2e;
            alternate-background-color: #20243a;
            border: 1px solid #2a2d3a;
            border-radius: 8px;
            color: #d0d4e4;
            gridline-color: #2a2d3a;
            font-size: 12px;
            selection-background-color: #3730a3;
            selection-color: white;
        }
        QHeaderView::section {
            background: #22253a;
            color: #8b8fa3;
            border: none;
            border-bottom: 2px solid #6c63ff;
            padding: 8px 6px;
            font-weight: 700;
            font-size: 11px;
            text-transform: uppercase;
        }

        QTextEdit {
            background: #12141e;
            color: #c8ccd8;
            border: 1px solid #2a2d3a;
            border-radius: 8px;
            font-family: 'Cascadia Code', 'Consolas', 'Courier New', monospace;
            font-size: 11px;
            padding: 8px;
        }

        QSpinBox, QDoubleSpinBox, QComboBox {
            background: #1c1f2e;
            border: 1px solid #2a2d3a;
            border-radius: 6px;
            padding: 6px 10px;
            color: #e0e4f0;
            font-size: 12px;
        }
        QSpinBox:focus, QDoubleSpinBox:focus, QComboBox:focus {
            border: 1px solid #6c63ff;
        }
        QComboBox::drop-down {
            border: none;
            width: 24px;
        }
        QComboBox QAbstractItemView {
            background: #1c1f2e;
            border: 1px solid #2a2d3a;
            color: #e0e4f0;
            selection-background-color: #3730a3;
        }

        QSlider::groove:horizontal {
            height: 6px;
            background: #2a2d3a;
            border-radius: 3px;
        }
        QSlider::handle:horizontal {
            background: #6c63ff;
            width: 16px;
            height: 16px;
            margin: -5px 0;
            border-radius: 8px;
        }
        QSlider::sub-page:horizontal {
            background: qlineargradient(x1:0, y1:0, x2:1, y2:0,
                stop:0 #6c63ff, stop:1 #8b5cf6);
            border-radius: 3px;
        }

        QSplitter::handle { background: #2a2d3a; height: 2px; }

        QCheckBox { color: #b0b4c8; spacing: 8px; }
        QCheckBox::indicator {
            width: 16px; height: 16px;
            border-radius: 4px;
            border: 2px solid #3a3d4e;
            background: #1c1f2e;
        }
        QCheckBox::indicator:checked {
            background: #6c63ff;
            border-color: #6c63ff;
        }

        QScrollBar:vertical {
            background: transparent;
            width: 8px;
        }
        QScrollBar::handle:vertical {
            background: #3a3d4e;
            border-radius: 4px;
            min-height: 30px;
        }
        QScrollBar::handle:vertical:hover { background: #5a5d6e; }
        QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical { height: 0; }
    )");

    auto* central = new QWidget(this);
    auto* mainLayout = new QVBoxLayout(central);
    mainLayout->setContentsMargins(16, 12, 16, 12);
    mainLayout->setSpacing(12);

    // ── HEADER BAR ──
    auto* headerBar = new QHBoxLayout();
    auto* logoLabel = new QLabel("◈", this);
    logoLabel->setStyleSheet("font-size: 28px; color: #6c63ff; margin-right: 4px;");
    auto* titleLabel = new QLabel("OpenDriver VR", this);
    titleLabel->setStyleSheet("font-size: 22px; font-weight: 800; color: #e0e4f0;");
    auto* versionLabel = new QLabel("v0.2.0", this);
    versionLabel->setStyleSheet("font-size: 11px; color: #6c63ff; font-weight: 600; "
                                 "background: #1c1f2e; border-radius: 4px; padding: 3px 8px;");
    headerBar->addWidget(logoLabel);
    headerBar->addWidget(titleLabel);
    headerBar->addWidget(versionLabel);
    headerBar->addStretch();
    mainLayout->addLayout(headerBar);

    // ── TABS ──
    m_tabs = new QTabWidget(this);
    mainLayout->addWidget(m_tabs);

    // ═══════════ OVERVIEW TAB ═══════════
    auto* overviewTab = new QWidget();
    auto* overviewLayout = new QVBoxLayout(overviewTab);
    overviewLayout->setContentsMargins(12, 12, 12, 12);
    overviewLayout->setSpacing(12);

    auto* devicesGroup = new QGroupBox("Connected Devices", overviewTab);
    auto* devicesGroupLayout = new QVBoxLayout(devicesGroup);
    m_deviceListView = new QListView(this);
    m_deviceListView->setModel(m_deviceModel);
    m_deviceListView->setAlternatingRowColors(true);
    m_deviceListView->setMinimumHeight(200);
    devicesGroupLayout->addWidget(m_deviceListView);
    overviewLayout->addWidget(devicesGroup);
    overviewLayout->addStretch();
    m_tabs->addTab(overviewTab, "⬡  Overview");

    // ═══════════ PLUGINS TAB ═══════════
    auto* pluginTab = new QWidget();
    auto* pluginLayout = new QVBoxLayout(pluginTab);
    pluginLayout->setContentsMargins(12, 12, 12, 12);
    pluginLayout->setSpacing(10);

    // Toolbar
    auto* toolbar = new QHBoxLayout();
    toolbar->setSpacing(8);
    m_searchEdit = new QLineEdit();
    m_searchEdit->setPlaceholderText("🔍  Search plugins...");
    m_searchEdit->setMinimumWidth(200);
    toolbar->addWidget(m_searchEdit);
    toolbar->addStretch();

    auto* btnEnableAll = new QPushButton("Enable All");
    btnEnableAll->setProperty("secondary", true);
    auto* btnDisableAll = new QPushButton("Disable All");
    btnDisableAll->setProperty("secondary", true);
    auto* btnRefresh = new QPushButton("↻  Refresh");
    auto* btnRemove = new QPushButton("✕  Remove");
    btnRemove->setProperty("danger", true);
    toolbar->addWidget(btnEnableAll);
    toolbar->addWidget(btnDisableAll);
    toolbar->addWidget(btnRefresh);
    toolbar->addWidget(btnRemove);
    pluginLayout->addLayout(toolbar);

    // Splitter
    auto* splitter = new QSplitter(Qt::Vertical, this);
    pluginLayout->addWidget(splitter);

    m_pluginTableView = new QTableView();
    m_pluginTableView->setModel(m_pluginModel);
    m_pluginTableView->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    m_pluginTableView->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_pluginTableView->setSelectionMode(QAbstractItemView::SingleSelection);
    m_pluginTableView->setAlternatingRowColors(true);
    m_pluginTableView->verticalHeader()->hide();
    m_pluginTableView->setShowGrid(false);
    splitter->addWidget(m_pluginTableView);

    // Log panel
    auto* logContainer = new QWidget();
    auto* logLayout = new QVBoxLayout(logContainer);
    logLayout->setContentsMargins(0, 8, 0, 0);

    auto* logHeader = new QHBoxLayout();
    m_logStatusLabel = new QLabel("◉  Logs: Showing all", this);
    m_logStatusLabel->setStyleSheet("font-weight: 700; color: #8b8fa3; font-size: 11px;");
    auto* btnClearLogs = new QPushButton("Clear");
    btnClearLogs->setProperty("secondary", true);
    btnClearLogs->setMaximumWidth(80);
    logHeader->addWidget(m_logStatusLabel);
    logHeader->addStretch();
    logHeader->addWidget(btnClearLogs);
    logLayout->addLayout(logHeader);

    m_logView = new QTextEdit();
    m_logView->setReadOnly(true);
    m_logView->setPlaceholderText("Select a plugin to see its logs...");
    logLayout->addWidget(m_logView);
    splitter->addWidget(logContainer);
    splitter->setStretchFactor(0, 3);
    splitter->setStretchFactor(1, 2);

    m_tabs->addTab(pluginTab, "⚙  Plugins");

    connect(btnRefresh, &QPushButton::clicked, this, &MainWindow::onRefreshPlugins);
    connect(btnRemove, &QPushButton::clicked, this, &MainWindow::onRemovePlugin);
    connect(btnClearLogs, &QPushButton::clicked, m_logView, &QTextEdit::clear);
    connect(m_pluginTableView, &QTableView::clicked, this, &MainWindow::onPluginTableClicked);
    connect(m_pluginTableView->selectionModel(), &QItemSelectionModel::selectionChanged, this, &MainWindow::onPluginSelected);
    connect(btnEnableAll, &QPushButton::clicked, this, &MainWindow::onEnableAll);
    connect(btnDisableAll, &QPushButton::clicked, this, &MainWindow::onDisableAll);

    // ═══════════ VIDEO ENCODING TAB ═══════════
    m_videoTab = new QWidget();
    auto* videoLayout = new QVBoxLayout(m_videoTab);
    videoLayout->setContentsMargins(12, 12, 12, 12);
    videoLayout->setSpacing(12);

    auto* videoGroup = new QGroupBox("H.264 Encoder Configuration", m_videoTab);
    auto* videoGroupLayout = new QVBoxLayout(videoGroup);
    videoGroupLayout->setSpacing(12);

    // Encoder type
    auto* encoderLayout = new QHBoxLayout();
    auto* encLabel = new QLabel("Encoder:");
    encLabel->setStyleSheet("font-weight: 600; color: #e0e4f0;");
    encoderLayout->addWidget(encLabel);
    m_encoderTypeCombo = new QComboBox();
    m_encoderTypeCombo->addItem("H.264 (Media Foundation) — Recommended");
    m_encoderTypeCombo->addItem("H.265 (HEVC) — Lower bandwidth");
    m_encoderTypeCombo->setMinimumWidth(300);
    encoderLayout->addWidget(m_encoderTypeCombo);
    encoderLayout->addStretch();
    videoGroupLayout->addLayout(encoderLayout);

    // Quality preset
    auto* qualityLayout = new QHBoxLayout();
    auto* qualLabel = new QLabel("Quality:");
    qualLabel->setStyleSheet("font-weight: 600; color: #e0e4f0;");
    qualityLayout->addWidget(qualLabel);
    m_qualityPresetCombo = new QComboBox();
    m_qualityPresetCombo->addItem("ultrafast (Lowest latency)");
    m_qualityPresetCombo->addItem("superfast");
    m_qualityPresetCombo->addItem("veryfast");
    m_qualityPresetCombo->addItem("faster");
    m_qualityPresetCombo->setCurrentIndex(0);
    m_qualityPresetCombo->setMinimumWidth(300);
    qualityLayout->addWidget(m_qualityPresetCombo);
    qualityLayout->addStretch();
    videoGroupLayout->addLayout(qualityLayout);

    // Bitrate
    auto* bitrateLayout = new QHBoxLayout();
    auto* brLabel = new QLabel("Bitrate:");
    brLabel->setStyleSheet("font-weight: 600; color: #e0e4f0;");
    bitrateLayout->addWidget(brLabel);
    m_bitrateSpinBox = new QSpinBox();
    m_bitrateSpinBox->setMinimum(5);
    m_bitrateSpinBox->setMaximum(100);
    m_bitrateSpinBox->setValue(30);
    m_bitrateSpinBox->setSingleStep(1);
    m_bitrateSpinBox->setSuffix(" Mbps");
    m_bitrateSpinBox->setMinimumWidth(120);
    bitrateLayout->addWidget(m_bitrateSpinBox);
    m_bitrateValueLabel = new QLabel("(30 Mbps)");
    m_bitrateValueLabel->setStyleSheet("color: #6c63ff; font-weight: 600;");
    bitrateLayout->addWidget(m_bitrateValueLabel);
    bitrateLayout->addStretch();
    videoGroupLayout->addLayout(bitrateLayout);

    auto* slider = new QSlider(Qt::Horizontal);
    slider->setMinimum(5);
    slider->setMaximum(100);
    slider->setValue(30);
    videoGroupLayout->addWidget(slider);

    connect(m_bitrateSpinBox, QOverload<int>::of(&QSpinBox::valueChanged), this, &MainWindow::onBitrateChanged);
    connect(slider, &QSlider::valueChanged, m_bitrateSpinBox, &QSpinBox::setValue);
    connect(m_bitrateSpinBox, QOverload<int>::of(&QSpinBox::valueChanged), slider, &QSlider::setValue);

    // Stats card
    m_encodingStatsLabel = new QLabel("⏳  Waiting for encoder data...");
    m_encodingStatsLabel->setStyleSheet(
        "background: #12141e; padding: 14px; border-radius: 8px; "
        "color: #8b8fa3; border: 1px solid #2a2d3a;");
    videoGroupLayout->addWidget(m_encodingStatsLabel);

    auto* btnApply = new QPushButton("✓  Apply Settings");
    connect(btnApply, &QPushButton::clicked, this, &MainWindow::onApplyVideoSettings);
    videoGroupLayout->addWidget(btnApply);

    videoGroupLayout->addStretch();
    videoLayout->addWidget(videoGroup);
    videoLayout->addStretch();

    int videoTabIndex = m_tabs->addTab(m_videoTab, "🎬  Video Encoding");
    m_tabs->removeTab(videoTabIndex);
    m_videoTabVisible = false;

    setCentralWidget(central);

    // Load initial video settings
    onLoadVideoSettings();
    syncPluginTabs();
}

void MainWindow::onRefreshTimer() {
    m_deviceModel->refresh();
    m_pluginModel->refresh();
    syncPluginTabs();

    opendriver::core::Event evt;
    if (m_runtime->GetEventBusPublic().GetLatestEventCopy(
            opendriver::core::EventType::UI_SHOW_ENCODER_SETTINGS, evt) &&
        evt.timestamp != 0 && evt.timestamp != m_lastShowEncoderUiEventTs) {
        m_lastShowEncoderUiEventTs = evt.timestamp;
        if (m_videoTab && !m_videoTabVisible) {
            m_tabs->addTab(m_videoTab, "🎬  Video Encoding");
            m_videoTabVisible = true;
        }
        if (m_videoTab) {
            m_tabs->setCurrentWidget(m_videoTab);
        }
    }
}

void MainWindow::onEnableAll() {
    auto reply = QMessageBox::warning(this, "Warning: SteamVR Stability",
        "Włączenie wszystkich wtyczek w locie może uszkodzić sesję SteamVR. "
        "Kontynuować (Apply)?",
        QMessageBox::Apply | QMessageBox::Cancel);
    if (reply == QMessageBox::Apply) {
        m_runtime->SetAllPluginsState(true);
        m_pluginModel->refresh();
    }
}

void MainWindow::onDisableAll() {
    auto reply = QMessageBox::warning(this, "Warning: SteamVR Stability",
        "Wyłączenie wszystkich wtyczek w locie może uszkodzić sesję SteamVR. "
        "Kontynuować (Apply)?",
        QMessageBox::Apply | QMessageBox::Cancel);
    if (reply == QMessageBox::Apply) {
        m_runtime->SetAllPluginsState(false);
        m_pluginModel->refresh();
    }
}

void MainWindow::onRefreshPlugins() {
    m_runtime->ReloadPlugins();
    m_pluginModel->refresh();
    syncPluginTabs();
}

void MainWindow::onPluginSelected(const QItemSelection& selected, const QItemSelection& /*deselected*/) {
    if (selected.isEmpty()) {
        m_selectedPlugin = "";
        m_logStatusLabel->setText("◉  Logs: Showing all");
        return;
    }
    
    int row = selected.indexes().first().row();
    m_selectedPlugin = m_pluginModel->index(row, 1).data().toString().toStdString(); // Index 1 is name
    
    m_logStatusLabel->setText(QString("◉  Logs: %1").arg(QString::fromStdString(m_selectedPlugin)));
    m_logView->append(QString("<br><i style='color: #6c63ff'>━━━ Logs for [%1] ━━━</i>").arg(QString::fromStdString(m_selectedPlugin)));
}

void MainWindow::onPluginTableClicked(const QModelIndex& index) {
    if (index.column() == 4) { // Kolumna "Logs"
        m_pluginTableView->selectRow(index.row());
        m_logView->setFocus();
    }
}

void MainWindow::onLogReceived(const opendriver::core::LogEntry& entry) {
    // Filtrowanie (lub pokazuj wszystko jeśli nic nie wybrano)
    if (!m_selectedPlugin.empty() && entry.source != m_selectedPlugin && entry.source != "core") {
        return;
    }

    QString timeStr = QDateTime::fromMSecsSinceEpoch(entry.timestamp).toString("hh:mm:ss.zzz");
    QString color = "#c8ccd8"; // default text
    
    if (entry.level >= opendriver::core::LogLevel::Error) color = "#f87171";
    else if (entry.level == opendriver::core::LogLevel::Warn) color = "#fbbf24";

    QString html = QString("<span style='color: #4b4f6a'>[%1]</span> <span style='color: %2'><b>[%3]</b> %4</span>")
        .arg(timeStr)
        .arg(color)
        .arg(QString::fromStdString(entry.source))
        .arg(QString::fromStdString(entry.message).toHtmlEscaped());

    m_logView->append(html);
    
    // Auto-scroll
    m_logView->moveCursor(QTextCursor::End);
}

void MainWindow::onBitrateChanged(int value) {
    m_bitrateValueLabel->setText(QString("(%1 Mbps)").arg(value));
}

void MainWindow::onEncoderTypeChanged(int index) {
    // Future: switch between x264 and x265
}

void MainWindow::onQualityPresetChanged(int index) {
    // Future: adjust encoding preset dynamically
}

void MainWindow::onLoadVideoSettings() {
    try {
        namespace fs = std::filesystem;
        std::string config_dir  = opendriver::core::GetDefaultConfigDir();
        std::string config_file = config_dir + "/config.json";
        
        if (fs::exists(config_file)) {
            std::ifstream f(config_file);
            nlohmann::json j = nlohmann::json::parse(f);
            
            if (j.contains("video_encoding")) {
                auto& ve = j["video_encoding"];
                int bitrate = ve.value("bitrate_mbps", 30);
                m_bitrateSpinBox->setValue(bitrate);
                
                std::string encoder = ve.value("encoder", "h264");
                if (encoder == "h265") {
                    m_encoderTypeCombo->setCurrentIndex(1);
                } else {
                    m_encoderTypeCombo->setCurrentIndex(0);
                }
                
                std::string preset = ve.value("preset", "ultrafast");
                int presetIdx = 0;
                if (preset == "superfast") presetIdx = 1;
                else if (preset == "veryfast") presetIdx = 2;
                else if (preset == "faster") presetIdx = 3;
                m_qualityPresetCombo->setCurrentIndex(presetIdx);
            }
        }
    } catch (const std::exception& e) {
        // Ignore errors - use defaults
    }
}

void MainWindow::onApplyVideoSettings() {
    try {
        namespace fs = std::filesystem;
        std::string config_dir  = opendriver::core::GetDefaultConfigDir();
        std::string config_file = config_dir + "/config.json";
        
        // Load existing config or create new
        nlohmann::json j;
        if (fs::exists(config_file)) {
            std::ifstream f(config_file);
            j = nlohmann::json::parse(f);
        }
        
        // Update video encoding settings
        j["video_encoding"]["bitrate_mbps"] = m_bitrateSpinBox->value();
        j["video_encoding"]["encoder"] = (m_encoderTypeCombo->currentIndex() == 0) ? "h264" : "h265";
        
        // Map preset index to name
        const char* presets[] = {"ultrafast", "superfast", "veryfast", "faster"};
        j["video_encoding"]["preset"] = presets[m_qualityPresetCombo->currentIndex()];
        
        // Ensure directory exists
        if (!fs::exists(config_dir)) {
            fs::create_directories(config_dir);
        }
        
        // Write config
        std::ofstream f(config_file);
        f << j.dump(2);
        f.close();
        
        QMessageBox::information(this, "Success", 
            QString("Video settings saved!\nBitrate: %1 Mbps\nPreset: %2")
            .arg(m_bitrateSpinBox->value())
            .arg(m_qualityPresetCombo->currentText()));
        
        // Logging
        opendriver::core::Logger::GetInstance().Info("UI", 
            "Video settings updated: bitrate=" + std::to_string(m_bitrateSpinBox->value()) + " Mbps");
    } catch (const std::exception& e) {
        QMessageBox::critical(this, "Error", QString("Failed to save settings: %1").arg(e.what()));
        opendriver::core::Logger::GetInstance().Error("UI", std::string("Failed to save video settings: ") + e.what());
    }
}

void MainWindow::syncPluginTabs() {
    if (!m_tabs) {
        return;
    }

    auto plugins = m_runtime->GetPluginLoader().GetPlugins();
    std::vector<std::string> desired_names;

    for (auto* plugin : plugins) {
        if (!plugin) {
            continue;
        }

        auto* provider = plugin->GetUIProvider();
        if (!provider) {
            continue;
        }

        const std::string plugin_name = plugin->GetName();
        desired_names.push_back(plugin_name);

        auto it = std::find_if(
            m_pluginTabs.begin(),
            m_pluginTabs.end(),
            [&](const PluginTab& tab) { return tab.plugin_name == plugin_name; });

        if (it == m_pluginTabs.end()) {
            QWidget* widget = provider->CreateSettingsWidget(m_tabs);
            m_tabs->addTab(widget, QString::fromStdString(plugin_name));
            m_pluginTabs.push_back({plugin_name, provider, widget});
        } else {
            it->provider = provider;
        }
    }

    for (auto it = m_pluginTabs.begin(); it != m_pluginTabs.end();) {
        if (std::find(desired_names.begin(), desired_names.end(), it->plugin_name) != desired_names.end()) {
            ++it;
            continue;
        }

        const int tab_index = m_tabs->indexOf(it->widget);
        if (tab_index >= 0) {
            m_tabs->removeTab(tab_index);
        }
        if (it->widget) {
            it->widget->deleteLater();
        }
        it = m_pluginTabs.erase(it);
    }

    for (auto& tab : m_pluginTabs) {
        if (tab.provider) {
            tab.provider->RefreshUI();
        }
    }
}

void MainWindow::onRemovePlugin() {
    if (m_selectedPlugin.empty()) {
        QMessageBox::information(this, "Remove Plugin", "Please select a plugin from the list first.");
        return;
    }

    std::string name = m_selectedPlugin;
    std::string pluginPath = "";
    
    auto available = m_runtime->GetAvailablePlugins();
    for (const auto& ap : available) {
        if (ap.name == name) {
            pluginPath = ap.path;
            break;
        }
    }

    if (pluginPath.empty()) {
        QMessageBox::critical(this, "Error", "Could not find storage path for selected plugin.");
        return;
    }

    auto reply = QMessageBox::critical(this, "Confirm Removal",
        QString("Are you sure you want to PERMANENTLY DELETE plugin '%1'?\n\nPath: %2\n\nThis action cannot be undone.")
        .arg(QString::fromStdString(name))
        .arg(QString::fromStdString(pluginPath)),
        QMessageBox::Yes | QMessageBox::No);

    if (reply == QMessageBox::Yes) {
        try {
            // 1. Disable/Unload
            m_runtime->DisablePlugin(name);
            
            // 2. Physically remove from disk
            std::filesystem::remove_all(pluginPath);
            
            QMessageBox::information(this, "Success", QString("Plugin '%1' has been removed from disk.").arg(QString::fromStdString(name)));
            
            onRefreshPlugins();
        } catch (const std::exception& e) {
            QMessageBox::critical(this, "Error", QString("Failed to remove plugin: %1").arg(e.what()));
        }
    }
}

} // namespace opendriver::ui
