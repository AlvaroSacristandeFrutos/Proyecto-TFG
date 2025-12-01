#include "MainWindow.h"
#include "ChipVisualizer.h"
#include "PinControlPanel.h"
#include "ConnectionDialog.h"

// No necesitamos incluir ScanController.h aquí porque ya está en el .h, 
// pero no hace daño dejarlo.

#include <QMenuBar>
#include <QMenu>
#include <QMessageBox>
#include <QFileDialog>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QSplitter>

MainWindow::MainWindow(QWidget* parent)
    : QMainWindow(parent)
    , m_chipVisualizer(nullptr)
    , m_pinControlPanel(nullptr)
    // --- CORRECCIÓN: Namespace JTAG:: explícito ---
    , m_controller(std::make_unique<JTAG::ScanController>())
    , m_mainToolBar(nullptr)
    , m_modeToolBar(nullptr)
    , m_connected(false)
    , m_deviceInitialized(false)
    , m_currentMode("None")
    , m_refreshTimer(new QTimer(this))
{
    setWindowTitle("JTAG Boundary Scanner - TopJTAG Style");
    setMinimumSize(1200, 800);

    createActions();
    createMenus();
    createToolBars();
    createDockWidgets();
    createStatusBar();

    m_chipVisualizer = new ChipVisualizer(this);
    setCentralWidget(m_chipVisualizer);

    connectBackend();

    connect(m_refreshTimer, &QTimer::timeout, this, &MainWindow::onRefreshPins);
    updateConnectionStatus();
}

MainWindow::~MainWindow() {
}

// ... (RESTO DEL CÓDIGO IGUAL QUE ANTES) ...
// Copia el resto de funciones (createActions, etc.) del MainWindow.cpp anterior.
// El único cambio crítico estaba en el constructor (línea 22) y los includes.

void MainWindow::createActions() {
    m_actConnect = new QAction(QIcon(), "Connect", this);
    m_actConnect->setStatusTip("Connect to JTAG adapter");
    connect(m_actConnect, &QAction::triggered, this, &MainWindow::onConnectAdapter);

    m_actDisconnect = new QAction(QIcon(), "Disconnect", this);
    m_actDisconnect->setStatusTip("Disconnect from adapter");
    m_actDisconnect->setEnabled(false);
    connect(m_actDisconnect, &QAction::triggered, this, &MainWindow::onDisconnect);

    m_actLoadBSDL = new QAction(QIcon(), "Load BSDL", this);
    m_actLoadBSDL->setStatusTip("Load BSDL file");
    connect(m_actLoadBSDL, &QAction::triggered, this, &MainWindow::onLoadBSDL);

    m_actInitDevice = new QAction(QIcon(), "Initialize Device", this);
    m_actInitDevice->setStatusTip("Initialize JTAG device");
    m_actInitDevice->setEnabled(false);
    connect(m_actInitDevice, &QAction::triggered, this, &MainWindow::onInitializeDevice);

    m_actSample = new QAction(QIcon(), "SAMPLE", this);
    m_actSample->setStatusTip("Enter SAMPLE mode (read pins)");
    m_actSample->setEnabled(false);
    connect(m_actSample, &QAction::triggered, this, &MainWindow::onEnterSample);

    m_actExtest = new QAction(QIcon(), "EXTEST", this);
    m_actExtest->setStatusTip("Enter EXTEST mode (control pins)");
    m_actExtest->setEnabled(false);
    connect(m_actExtest, &QAction::triggered, this, &MainWindow::onEnterExtest);

    m_actBypass = new QAction(QIcon(), "BYPASS", this);
    m_actBypass->setStatusTip("Enter BYPASS mode");
    m_actBypass->setEnabled(false);
    connect(m_actBypass, &QAction::triggered, this, &MainWindow::onEnterBypass);

    m_actResetTAP = new QAction(QIcon(), "Reset TAP", this);
    m_actResetTAP->setStatusTip("Reset TAP state machine");
    m_actResetTAP->setEnabled(false);
    connect(m_actResetTAP, &QAction::triggered, this, &MainWindow::onResetTAP);

    m_actRefresh = new QAction(QIcon(), "Refresh", this);
    m_actRefresh->setStatusTip("Refresh pin states");
    m_actRefresh->setEnabled(false);
    connect(m_actRefresh, &QAction::triggered, this, &MainWindow::onRefreshPins);

    m_actAutoRefresh = new QAction(QIcon(), "Auto Refresh", this);
    m_actAutoRefresh->setStatusTip("Enable automatic pin state updates");
    m_actAutoRefresh->setCheckable(true);
    m_actAutoRefresh->setEnabled(false);
    connect(m_actAutoRefresh, &QAction::toggled, this, &MainWindow::onAutoRefreshToggled);

    m_actAbout = new QAction("About", this);
    m_actAbout->setStatusTip("About this application");
    connect(m_actAbout, &QAction::triggered, this, &MainWindow::onAbout);
}

void MainWindow::createMenus() {
    QMenu* fileMenu = menuBar()->addMenu("&File");
    fileMenu->addAction(m_actConnect);
    fileMenu->addAction(m_actDisconnect);
    fileMenu->addSeparator();
    fileMenu->addAction(m_actLoadBSDL);
    fileMenu->addSeparator();
    fileMenu->addAction("E&xit", this, &QWidget::close);

    QMenu* deviceMenu = menuBar()->addMenu("&Device");
    deviceMenu->addAction(m_actInitDevice);
    deviceMenu->addAction(m_actResetTAP);

    QMenu* modeMenu = menuBar()->addMenu("&Mode");
    modeMenu->addAction(m_actSample);
    modeMenu->addAction(m_actExtest);
    modeMenu->addAction(m_actBypass);

    QMenu* viewMenu = menuBar()->addMenu("&View");
    viewMenu->addAction(m_actRefresh);
    viewMenu->addAction(m_actAutoRefresh);

    QMenu* helpMenu = menuBar()->addMenu("&Help");
    helpMenu->addAction(m_actAbout);
}

void MainWindow::createToolBars() {
    m_mainToolBar = addToolBar("Main");
    m_mainToolBar->setMovable(false);
    m_mainToolBar->addAction(m_actConnect);
    m_mainToolBar->addAction(m_actDisconnect);
    m_mainToolBar->addSeparator();
    m_mainToolBar->addAction(m_actLoadBSDL);
    m_mainToolBar->addAction(m_actInitDevice);
    m_mainToolBar->addSeparator();
    m_mainToolBar->addAction(m_actRefresh);
    m_mainToolBar->addAction(m_actAutoRefresh);

    m_modeToolBar = addToolBar("JTAG Mode");
    m_modeToolBar->setMovable(false);
    m_modeToolBar->addAction(m_actSample);
    m_modeToolBar->addAction(m_actExtest);
    m_modeToolBar->addAction(m_actBypass);
    m_modeToolBar->addSeparator();
    m_modeToolBar->addAction(m_actResetTAP);
}

void MainWindow::createDockWidgets() {
    QDockWidget* pinDock = new QDockWidget("Pin Control", this);
    pinDock->setAllowedAreas(Qt::RightDockWidgetArea | Qt::LeftDockWidgetArea);
    m_pinControlPanel = new PinControlPanel(pinDock);
    pinDock->setWidget(m_pinControlPanel);
    addDockWidget(Qt::RightDockWidgetArea, pinDock);

    connect(m_pinControlPanel, &PinControlPanel::pinChanged,
        this, &MainWindow::onPinChanged);
    connect(m_pinControlPanel, &PinControlPanel::busWrite,
        this, &MainWindow::onBusWrite);
}

void MainWindow::createStatusBar() {
    m_statusConnection = new QLabel("Disconnected");
    m_statusConnection->setFrameStyle(QFrame::Panel | QFrame::Sunken);
    m_statusConnection->setMinimumWidth(150);
    statusBar()->addPermanentWidget(m_statusConnection);

    m_statusDevice = new QLabel("No Device");
    m_statusDevice->setFrameStyle(QFrame::Panel | QFrame::Sunken);
    m_statusDevice->setMinimumWidth(200);
    statusBar()->addPermanentWidget(m_statusDevice);

    m_statusMode = new QLabel("Mode: None");
    m_statusMode->setFrameStyle(QFrame::Panel | QFrame::Sunken);
    m_statusMode->setMinimumWidth(120);
    statusBar()->addPermanentWidget(m_statusMode);

    statusBar()->showMessage("Ready");
}

void MainWindow::connectBackend() {
}

void MainWindow::updateConnectionStatus() {
    if (m_connected) {
        m_statusConnection->setText("Connected");
        m_statusConnection->setStyleSheet("QLabel { background-color: #90EE90; }");
        m_actConnect->setEnabled(false);
        m_actDisconnect->setEnabled(true);
        m_actInitDevice->setEnabled(true);
    }
    else {
        m_statusConnection->setText("Disconnected");
        m_statusConnection->setStyleSheet("QLabel { background-color: #FFB6C1; }");
        m_actConnect->setEnabled(true);
        m_actDisconnect->setEnabled(false);
        m_actInitDevice->setEnabled(false);
        m_actSample->setEnabled(false);
        m_actExtest->setEnabled(false);
        m_actBypass->setEnabled(false);
        m_actResetTAP->setEnabled(false);
        m_actRefresh->setEnabled(false);
        m_actAutoRefresh->setEnabled(false);
    }
}

void MainWindow::updateDeviceInfo() {
    if (m_deviceInitialized) {
        QString deviceName = QString::fromStdString(m_controller->getDeviceName());
        if (deviceName.isEmpty()) deviceName = "Unknown Device";

        uint32_t idcode = m_controller->getIDCODE();
        m_statusDevice->setText(QString("%1 (0x%2)")
            .arg(deviceName)
            .arg(idcode, 8, 16, QChar('0')));
        m_statusDevice->setStyleSheet("QLabel { background-color: #ADD8E6; }");

        m_actSample->setEnabled(true);
        m_actExtest->setEnabled(true);
        m_actBypass->setEnabled(true);
        m_actResetTAP->setEnabled(true);
    }
    else {
        m_statusDevice->setText("No Device");
        m_statusDevice->setStyleSheet("");
    }
}

void MainWindow::updatePinStates() {
    if (!m_deviceInitialized || !m_controller) {
        return;
    }

    m_controller->samplePins();

    std::vector<std::string> pins = m_controller->getPinList();
    QList<PinState> pinStates;
    for (const auto& pinNameStd : pins) {
        QString pinName = QString::fromStdString(pinNameStd);
        auto levelOpt = m_controller->getPin(pinNameStd);
        bool isHigh = false;
        if (levelOpt.has_value()) {
            isHigh = (levelOpt.value() == JTAG::PinLevel::HIGH);
        }

        PinState state;
        state.name = pinName;
        state.level = isHigh;
        state.enabled = (m_currentMode == "EXTEST");

        pinStates.append(state);
    }

    m_pinControlPanel->updatePins(pinStates);
    m_chipVisualizer->updatePinStates(pinStates);
}

void MainWindow::onConnectAdapter() {
    ConnectionDialog dialog(this);
    if (dialog.exec() == QDialog::Accepted) {
        JTAG::AdapterType type = dialog.getSelectedAdapter();

        if (m_controller->connectAdapter(type)) {
            m_connected = true;
            updateConnectionStatus();
            statusBar()->showMessage("Adapter connected successfully", 3000);
        }
        else {
            QMessageBox::critical(this, "Connection Error",
                "Failed to connect to adapter.\n"
                "Make sure the adapter is connected and drivers are installed.");
        }
    }
}

void MainWindow::onDisconnect() {
    if (m_refreshTimer->isActive()) {
        m_refreshTimer->stop();
        m_actAutoRefresh->setChecked(false);
    }

    m_connected = false;
    m_deviceInitialized = false;
    m_currentMode = "None";

    updateConnectionStatus();
    updateDeviceInfo();

    m_pinControlPanel->clearPins();
    m_chipVisualizer->clearPins();

    statusBar()->showMessage("Disconnected", 3000);
}

void MainWindow::onLoadBSDL() {
    QString fileName = QFileDialog::getOpenFileName(
        this,
        "Load BSDL File",
        "",
        "BSDL Files (*.bsd *.bsdl);;All Files (*)"
    );

    if (!fileName.isEmpty()) {
        if (m_controller->loadDeviceModel(fileName.toStdString())) {
            statusBar()->showMessage("BSDL loaded successfully", 3000);
            if (m_connected) updateDeviceInfo();

            // Layout visualizer
            m_chipVisualizer->createLayoutFromController(m_controller->getPinList());

        }
        else {
            QMessageBox::critical(this, "BSDL Error", "Failed to load BSDL file.");
        }
    }
}

void MainWindow::onInitializeDevice() {
    if (!m_connected) {
        QMessageBox::warning(this, "Not Connected", "Please connect to an adapter first.");
        return;
    }

    m_controller->detectDevice();

    if (m_controller->initializeDevice()) {
        m_deviceInitialized = true;
        updateDeviceInfo();
        statusBar()->showMessage("Device initialized successfully", 3000);
        updatePinStates();
    }
    else {
        QMessageBox::critical(this, "Initialization Error",
            "Failed to initialize device.\nCheck connections.");
    }
}

void MainWindow::onResetTAP() {
    m_controller->reset();
    statusBar()->showMessage("TAP reset", 2000);
}

void MainWindow::onEnterSample() {
    if (m_controller->enterSAMPLE()) {
        m_currentMode = "SAMPLE";
        m_statusMode->setText("Mode: SAMPLE");
        m_statusMode->setStyleSheet("QLabel { background-color: #FFFFE0; }");
        statusBar()->showMessage("Entered SAMPLE mode", 2000);
        m_actRefresh->setEnabled(true);
        m_actAutoRefresh->setEnabled(true);
        updatePinStates();
    }
    else {
        QMessageBox::critical(this, "Mode Error", "Failed to enter SAMPLE mode.");
    }
}

void MainWindow::onEnterExtest() {
    if (m_controller->enterEXTEST()) {
        m_currentMode = "EXTEST";
        m_statusMode->setText("Mode: EXTEST");
        m_statusMode->setStyleSheet("QLabel { background-color: #FFD700; }");
        statusBar()->showMessage("Entered EXTEST mode - Pin control enabled", 2000);
        m_actRefresh->setEnabled(true);
        m_actAutoRefresh->setEnabled(true);
        updatePinStates();
    }
    else {
        QMessageBox::critical(this, "Mode Error", "Failed to enter EXTEST mode.");
    }
}

void MainWindow::onEnterBypass() {
    if (m_controller->enterBYPASS()) {
        m_currentMode = "BYPASS";
        m_statusMode->setText("Mode: BYPASS");
        m_statusMode->setStyleSheet("QLabel { background-color: #D3D3D3; }");
        statusBar()->showMessage("Entered BYPASS mode", 2000);
        m_actRefresh->setEnabled(false);
        m_actAutoRefresh->setEnabled(false);
        if (m_refreshTimer->isActive()) {
            m_refreshTimer->stop();
            m_actAutoRefresh->setChecked(false);
        }
    }
    else {
        QMessageBox::critical(this, "Mode Error", "Failed to enter BYPASS mode.");
    }
}

void MainWindow::onPinChanged(const QString& pinName, bool level) {
    if (m_currentMode != "EXTEST") return;
    JTAG::PinLevel pinLevel = level ? JTAG::PinLevel::HIGH : JTAG::PinLevel::LOW;
    if (m_controller->setPin(pinName.toStdString(), pinLevel)) {
        updatePinStates();
    }
}

void MainWindow::onBusWrite(const QStringList& pins, uint32_t value) {
    if (m_currentMode != "EXTEST") return;
    std::vector<std::string> pinVec;
    for (const auto& pin : pins) pinVec.push_back(pin.toStdString());
    if (m_controller->writeBus(pinVec, static_cast<uint32_t>(value))) {
        updatePinStates();
        statusBar()->showMessage(QString("Bus write: 0x%1").arg(value, 0, 16), 2000);
    }
}

void MainWindow::onRefreshPins() {
    if (m_deviceInitialized && (m_currentMode == "SAMPLE" || m_currentMode == "EXTEST")) {
        updatePinStates();
    }
}

void MainWindow::onAutoRefreshToggled(bool enabled) {
    if (enabled) {
        m_refreshTimer->start(100);
        statusBar()->showMessage("Auto-refresh enabled", 2000);
    }
    else {
        m_refreshTimer->stop();
        statusBar()->showMessage("Auto-refresh disabled", 2000);
    }
}

void MainWindow::onAbout() {
    QMessageBox::about(this, "About JTAG Boundary Scanner",
        "<h2>JTAG Boundary Scanner</h2><p>TopJTAG-style tool (Qt6 C++)</p>");
}