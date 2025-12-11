#include "mainwindow.h"
#include "ui_mainwindow.h"
#include <QFileDialog>
#include <QMessageBox>
#include <QGraphicsRectItem>
#include <QGraphicsTextItem>
#include <QToolButton>
#include <QWidgetAction>
#include <QInputDialog>
#include <QHeaderView>
#include <QRadioButton>
#include <QButtonGroup>
#include <QPushButton>
#include <QLabel>
#include <iostream>
#include <iomanip>
#include <QMetaType>

// IMPORTANTE: Descomenta estas líneas cuando integres el backend
#include "../controller/ScanController.h"
#include "../hal/JtagProtocol.h"  // Para PinLevel enum
#include "ConnectionDialog.h"
#include "ChainExamineDialog.h"
#include "NewProjectWizard.h"

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
    , ui(new Ui::MainWindow)
    , scanController(nullptr)  // AQUÍ INICIALIZARÁS: std::make_unique<JTAG::ScanController>()
    , waveformScene(nullptr)
    , chipVisualizer(nullptr)
    , zoomComboBox(nullptr)
    , radioSample(nullptr)
    , radioExtest(nullptr)
    , radioBypass(nullptr)
    , jtagModeButtonGroup(nullptr)
    , btnSetAllSafe(nullptr)
    , btnSetAll1(nullptr)
    , btnSetAllZ(nullptr)
    , btnSetAll0(nullptr)
    , inoutActionGroup(nullptr)
    , currentZoom(1.0)
    , isAdapterConnected(false)
    , isDeviceDetected(false)
    , isDeviceInitialized(false)
    , currentJTAGMode(JTAGMode::SAMPLE)
    , isCapturing(false)
    , waveformTimebase(1.0)
{
    ui->setupUi(this);
    
    qRegisterMetaType<std::vector<JTAG::PinLevel>>("std::vector<JTAG::PinLevel>");

    initializeUI();
    setupGraphicsViews();
    setupTables();
    setupToolbar();
    setupBackend();
    setupConnections();
    
    // Hide Watch panel by default
    ui->dockWatch->setVisible(false);
    ui->actionWatch->setChecked(false);
    
    updateWindowTitle();
    enableControlsAfterConnection(false);
}

MainWindow::~MainWindow()
{
    // Detener polling si está activo
    if (scanController && isCapturing) {
        scanController->stopPolling();
    }
    delete waveformScene;
    delete ui;
}

void MainWindow::initializeUI()
{
    resize(1200, 800);
    updateStatusBar("Ready");
}

void MainWindow::setupBackend()
{
    scanController = std::make_unique<JTAG::ScanController>();

    if (!scanController) {
        QMessageBox::critical(this, "Initialization Error",
            "Failed to create ScanController");
        return;
    }
    // ================================================================

    // === CRÍTICO: Conectar señales del ScanController al MainWindow ===
    connect(scanController.get(), &JTAG::ScanController::pinsDataReady,
            this, &MainWindow::onPinsDataReady);
    connect(scanController.get(), &JTAG::ScanController::errorOccurred,
            this, &MainWindow::onScanError);
    // ===================================================================

    // Inicializar catálogo BSDL
    QString testFilesPath = QCoreApplication::applicationDirPath() + "/../../test_files";
    scanController->initializeBSDLCatalog(testFilesPath.toStdString());

    // NOTA: El polling ahora lo maneja ScanWorker en thread separado
    // El worker emite señales que llegan aquí vía las conexiones de arriba
    // No se usa QTimer - el worker emite señales automáticamente cada 100ms
}

void MainWindow::setupGraphicsViews()
{
    // Setup central chip visualizer (replacing standard QGraphicsView)
    chipVisualizer = new ChipVisualizer(this);

    // Replace the graphicsView widget with our ChipVisualizer
    QWidget *oldWidget = ui->graphicsView;
    QLayout *layout = oldWidget->parentWidget()->layout();
    if (layout) {
        layout->replaceWidget(oldWidget, chipVisualizer);
        oldWidget->hide();
    }

    // Setup waveform graphics view
    waveformScene = new QGraphicsScene(this);
    ui->graphicsViewWaveform->setScene(waveformScene);
    ui->graphicsViewWaveform->setRenderHint(QPainter::Antialiasing);
    
    // Add grid lines for waveform
    QPen gridPen(QColor(220, 220, 220));
    for (int i = 0; i < 20; i++) {
        waveformScene->addLine(i * 50, 0, i * 50, 200, gridPen);
    }
}

void MainWindow::setupTables()
{
    // Setup Pins table
    ui->tableWidgetPins->setColumnCount(5);
    ui->tableWidgetPins->setHorizontalHeaderLabels(
        QStringList() << "Name" << "Pin #" << "Port" << "I/O Value" << "Type");
    ui->tableWidgetPins->setSelectionBehavior(QAbstractItemView::SelectRows);
    ui->tableWidgetPins->setSelectionMode(QAbstractItemView::ExtendedSelection);

    // Permitir redimensionamiento manual en todas las columnas
    ui->tableWidgetPins->horizontalHeader()->setSectionResizeMode(QHeaderView::Interactive);
    // Establecer anchos iniciales
    ui->tableWidgetPins->setColumnWidth(0, 120);  // Name
    ui->tableWidgetPins->setColumnWidth(1, 60);   // Pin #
    ui->tableWidgetPins->setColumnWidth(2, 80);   // Port
    ui->tableWidgetPins->setColumnWidth(3, 80);   // I/O Value
    ui->tableWidgetPins->setColumnWidth(4, 80);   // Type

    // Setup Watch table
    ui->tableWidgetWatch->setColumnCount(6);
    ui->tableWidgetWatch->setHorizontalHeaderLabels(
        QStringList() << "Name" << "Pin #" << "Port" << "I/O Value" << "Transitions Count" << "Type");
    ui->tableWidgetWatch->setSelectionBehavior(QAbstractItemView::SelectRows);

    // Permitir redimensionamiento manual en todas las columnas
    ui->tableWidgetWatch->horizontalHeader()->setSectionResizeMode(QHeaderView::Interactive);
    // Establecer anchos iniciales
    ui->tableWidgetWatch->setColumnWidth(0, 120);  // Name
    ui->tableWidgetWatch->setColumnWidth(1, 60);   // Pin #
    ui->tableWidgetWatch->setColumnWidth(2, 80);   // Port
    ui->tableWidgetWatch->setColumnWidth(3, 80);   // I/O Value
    ui->tableWidgetWatch->setColumnWidth(4, 140);  // Transitions Count
    ui->tableWidgetWatch->setColumnWidth(5, 80);   // Type

    // Conectar señales de tabla de pines
    connect(ui->tableWidgetPins, &QTableWidget::itemChanged,
            this, &MainWindow::onPinTableItemChanged);
    connect(ui->tableWidgetPins->selectionModel(), &QItemSelectionModel::selectionChanged,
            this, &MainWindow::onPinTableSelectionChanged);

    // Setup Waveform table
    ui->tableWidgetWaveform->setColumnCount(5);
    ui->tableWidgetWaveform->setHorizontalHeaderLabels(
        QStringList() << "Name" << "Device" << "Pin #" << "Port" << "Type");
    ui->tableWidgetWaveform->setSelectionBehavior(QAbstractItemView::SelectRows);

    // Permitir redimensionamiento manual en todas las columnas
    ui->tableWidgetWaveform->horizontalHeader()->setSectionResizeMode(QHeaderView::Interactive);
    // Establecer anchos iniciales
    ui->tableWidgetWaveform->setColumnWidth(0, 120);  // Name
    ui->tableWidgetWaveform->setColumnWidth(1, 90);   // Device
    ui->tableWidgetWaveform->setColumnWidth(2, 60);   // Pin #
    ui->tableWidgetWaveform->setColumnWidth(3, 60);   // Port
    ui->tableWidgetWaveform->setColumnWidth(4, 80);   // Type
}

void MainWindow::setupToolbar()
{
    // Create zoom combobox for toolbar
    zoomComboBox = new QComboBox(this);
    zoomComboBox->addItems(QStringList() << "75%" << "100%" << "150%" << "200%");
    zoomComboBox->setCurrentIndex(1); // Default 100%
    zoomComboBox->setMinimumWidth(80);
    
    // Replace the zoom action with the combobox
    QWidgetAction *zoomWidgetAction = new QWidgetAction(this);
    zoomWidgetAction->setDefaultWidget(zoomComboBox);
    
    // Find the zoom action and replace it
    QList<QAction*> actions = ui->toolBar->actions();
    for (int i = 0; i < actions.size(); i++) {
        if (actions[i] == ui->actionZoom) {
            ui->toolBar->insertAction(ui->actionZoom, zoomWidgetAction);
            ui->toolBar->removeAction(ui->actionZoom);
            break;
        }
    }
    
    connect(zoomComboBox, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &MainWindow::onZoom);
    
    // Setup action group for IN/OUT of inout (radio button behavior)
    inoutActionGroup = new QActionGroup(this);
    inoutActionGroup->addAction(ui->actionIN_of_inout);
    inoutActionGroup->addAction(ui->actionOUT_of_inout);
    ui->actionIN_of_inout->setChecked(true);

    // === JTAG MODE SELECTOR ===
    ui->toolBar->addSeparator();

    // Add label
    QLabel *modeLabel = new QLabel(" Mode: ", this);
    ui->toolBar->addWidget(modeLabel);

    // Create radio buttons
    radioSample = new QRadioButton("SAMPLE", this);
    radioExtest = new QRadioButton("EXTEST", this);
    radioBypass = new QRadioButton("BYPASS", this);

    radioSample->setChecked(true); // Default to SAMPLE mode

    // Create button group
    jtagModeButtonGroup = new QButtonGroup(this);
    jtagModeButtonGroup->addButton(radioSample, 0);
    jtagModeButtonGroup->addButton(radioExtest, 1);
    jtagModeButtonGroup->addButton(radioBypass, 2);

    // Add to toolbar
    ui->toolBar->addWidget(radioSample);
    ui->toolBar->addWidget(radioExtest);
    ui->toolBar->addWidget(radioBypass);

    // Connect signal (use idClicked which passes the button ID directly)
    connect(jtagModeButtonGroup, &QButtonGroup::idClicked,
            this, &MainWindow::onJTAGModeChanged);

    // === QUICK ACTION BUTTONS ===
    ui->toolBar->addSeparator();

    btnSetAllSafe = new QPushButton("Safe State", this);
    btnSetAll1 = new QPushButton("All 1", this);
    btnSetAllZ = new QPushButton("All Z", this);
    btnSetAll0 = new QPushButton("All 0", this);

    // Set button tooltips
    btnSetAllSafe->setToolTip("Set all pins to BSDL safe values");
    btnSetAll1->setToolTip("Set all output pins to HIGH");
    btnSetAllZ->setToolTip("Set all output pins to High-Z");
    btnSetAll0->setToolTip("Set all output pins to LOW");

    // Add to toolbar
    ui->toolBar->addWidget(btnSetAllSafe);
    ui->toolBar->addWidget(btnSetAll1);
    ui->toolBar->addWidget(btnSetAllZ);
    ui->toolBar->addWidget(btnSetAll0);

    // Connect signals
    connect(btnSetAllSafe, &QPushButton::clicked, this, &MainWindow::onSetAllToSafeState);
    connect(btnSetAll1, &QPushButton::clicked, this, &MainWindow::onSetAllTo1);
    connect(btnSetAllZ, &QPushButton::clicked, this, &MainWindow::onSetAllToZ);
    connect(btnSetAll0, &QPushButton::clicked, this, &MainWindow::onSetAllTo0);

    // Initially disable these buttons (enable after connection)
    radioSample->setEnabled(false);
    radioExtest->setEnabled(false);
    radioBypass->setEnabled(false);
    btnSetAllSafe->setEnabled(false);
    btnSetAll1->setEnabled(false);
    btnSetAllZ->setEnabled(false);
    btnSetAll0->setEnabled(false);
}

void MainWindow::setupConnections()
{
    // File menu connections
    connect(ui->actionNew_Project_Wizard, &QAction::triggered, this, &MainWindow::onNewProjectWizard);
    connect(ui->actionOpen, &QAction::triggered, this, &MainWindow::onOpen);
    connect(ui->actionSave, &QAction::triggered, this, &MainWindow::onSave);
    connect(ui->actionSave_As, &QAction::triggered, this, &MainWindow::onSaveAs);
    connect(ui->actionExit, &QAction::triggered, this, &MainWindow::onExit);
    
    // View menu connections
    connect(ui->actionIN_of_inout, &QAction::triggered, [this]() { onInoutPinsDisplaying(true); });
    connect(ui->actionOUT_of_inout, &QAction::triggered, [this]() { onInoutPinsDisplaying(false); });
    connect(ui->actionZoom_Menu, &QAction::triggered, this, &MainWindow::onZoom);
    
    // Scan menu connections
    connect(ui->actionJTAG_Connection, &QAction::triggered, this, &MainWindow::onJTAGConnection);
    connect(ui->actionExamine_Chain, &QAction::triggered, this, &MainWindow::onExamineChain);
    connect(ui->actionRun, &QAction::triggered, this, &MainWindow::onRun);
    connect(ui->actionJTAG_Reset, &QAction::triggered, this, &MainWindow::onJTAGReset);
    connect(ui->actionDevice_Instruction, &QAction::triggered, this, &MainWindow::onDeviceInstruction);
    connect(ui->actionDevice_BSDL_File, &QAction::triggered, this, &MainWindow::onDeviceBSDLFile);
    connect(ui->actionDevice_Package, &QAction::triggered, this, &MainWindow::onDevicePackage);
    connect(ui->actionDevice_Properties, &QAction::triggered, this, &MainWindow::onDeviceProperties);
    
    // Pins menu connections
    connect(ui->actionSearch_Pins, &QAction::triggered, this, &MainWindow::onSearchPins);
    connect(ui->actionEdit_Pin_Names_and_Buses, &QAction::triggered, this, &MainWindow::onEditPinNamesAndBuses);
    connect(ui->actionSet_to_0, &QAction::triggered, this, &MainWindow::onSetTo0);
    connect(ui->actionSet_to_1, &QAction::triggered, this, &MainWindow::onSetTo1);
    connect(ui->actionSet_to_Z, &QAction::triggered, this, &MainWindow::onSetToZ);
    connect(ui->actionToggle, &QAction::triggered, this, &MainWindow::onTogglePinValue);
    connect(ui->actionSet_Bus_Value, &QAction::triggered, this, &MainWindow::onSetBusValue);
    connect(ui->actionSet_Bus_to_All_Z, &QAction::triggered, this, &MainWindow::onSetBusToAllZ);
    connect(ui->actionSet_All_Device_Pins_to_BSDL_Safe, &QAction::triggered, this, &MainWindow::onSetAllDevicePinsToBSDLSafe);
    
    // Watch menu connections
    connect(ui->actionWatch_Show, &QAction::triggered, this, &MainWindow::onWatchShow);
    connect(ui->actionWatch_Add_Signal, &QAction::triggered, this, &MainWindow::onWatchAddSignal);
    connect(ui->actionWatch_Remove, &QAction::triggered, this, &MainWindow::onWatchRemove);
    connect(ui->actionWatch_Remove_All, &QAction::triggered, this, &MainWindow::onWatchRemoveAll);
    connect(ui->actionWatch_Zero_Transition_Counter, &QAction::triggered, this, &MainWindow::onWatchZeroTransitionCounter);
    connect(ui->actionWatch_Zero_All_Transition_Counters, &QAction::triggered, this, &MainWindow::onWatchZeroAllTransitionCounters);
    
    // Waveform menu connections
    connect(ui->actionWaveform_Close, &QAction::triggered, this, &MainWindow::onWaveformClose);
    connect(ui->actionWaveform_Add_Signal, &QAction::triggered, this, &MainWindow::onWaveformAddSignal);
    connect(ui->actionWaveform_Remove, &QAction::triggered, this, &MainWindow::onWaveformRemove);
    connect(ui->actionWaveform_Remove_All, &QAction::triggered, this, &MainWindow::onWaveformRemoveAll);
    connect(ui->actionWaveform_Clear, &QAction::triggered, this, &MainWindow::onWaveformClear);
    connect(ui->actionWaveform_Zoom, &QAction::triggered, this, &MainWindow::onWaveformZoom);
    connect(ui->actionWaveform_Zoom_In, &QAction::triggered, this, &MainWindow::onWaveformZoomIn);
    connect(ui->actionWaveform_Zoom_Out, &QAction::triggered, this, &MainWindow::onWaveformZoomOut);
    connect(ui->actionWaveform_Go_to_Time, &QAction::triggered, this, &MainWindow::onWaveformGoToTime);
    connect(ui->actionWaveform_Previous_Event, &QAction::triggered, this, &MainWindow::onWaveformPreviousEvent);
    connect(ui->actionWaveform_Next_Event, &QAction::triggered, this, &MainWindow::onWaveformNextEvent);
    
    // Help menu connections
    connect(ui->actionHelp_Contents, &QAction::triggered, this, &MainWindow::onHelpContents);
    connect(ui->actionTurn_On_Logging, &QAction::triggered, this, &MainWindow::onTurnOnLogging);
    connect(ui->actionRegister, &QAction::triggered, this, &MainWindow::onRegister);
    connect(ui->actionAbout, &QAction::triggered, this, &MainWindow::onAbout);
    
    // Toolbar connections
    connect(ui->actionInstruction, &QAction::triggered, this, &MainWindow::onInstruction);
    
    // Pins panel connections
    connect(ui->comboBoxDevice, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &MainWindow::onDeviceChanged);
    connect(ui->toolButtonSearchPins, &QToolButton::clicked, this, &MainWindow::onSearchPinsButton);
    connect(ui->lineEditSearchPins, &QLineEdit::returnPressed, this, &MainWindow::onSearchPinsButton);
    connect(ui->tableWidgetPins, &QTableWidget::itemSelectionChanged, this, &MainWindow::onPinTableSelectionChanged);

    // Waveform toolbar connections
    connect(ui->actionWaveZoomIn, &QAction::triggered, this, &MainWindow::onWaveZoomIn);
    connect(ui->actionWaveZoomOut, &QAction::triggered, this, &MainWindow::onWaveZoomOut);
    connect(ui->actionWaveFit, &QAction::triggered, this, &MainWindow::onWaveFit);
    connect(ui->actionWavePrev, &QAction::triggered, this, &MainWindow::onWavePrev);
    connect(ui->actionWaveNext, &QAction::triggered, this, &MainWindow::onWaveNext);
    connect(ui->actionWaveGoto, &QAction::triggered, this, &MainWindow::onWaveGoto);
}

void MainWindow::enableControlsAfterConnection(bool enable)
{
    // Enable/disable controls based on connection state
    ui->actionRun->setEnabled(enable && isDeviceInitialized);
    ui->actionJTAG_Reset->setEnabled(enable);
    ui->actionExamine_Chain->setEnabled(enable);
    ui->actionDevice_Instruction->setEnabled(enable && isDeviceDetected);
    ui->actionDevice_BSDL_File->setEnabled(enable);
    ui->actionDevice_Properties->setEnabled(enable && isDeviceDetected);
    
    // Pin operations require initialized device
    ui->actionSet_to_0->setEnabled(enable && isDeviceInitialized);
    ui->actionSet_to_1->setEnabled(enable && isDeviceInitialized);
    ui->actionSet_to_Z->setEnabled(enable && isDeviceInitialized);
    ui->actionToggle->setEnabled(enable && isDeviceInitialized);
    ui->actionSet_Bus_Value->setEnabled(enable && isDeviceInitialized);

    // Enable JTAG mode selector and quick action buttons
    radioSample->setEnabled(enable && isDeviceInitialized);
    radioExtest->setEnabled(enable && isDeviceInitialized);
    radioBypass->setEnabled(enable && isDeviceInitialized);
    btnSetAllSafe->setEnabled(enable && isDeviceInitialized);
    btnSetAll1->setEnabled(enable && isDeviceInitialized);
    btnSetAllZ->setEnabled(enable && isDeviceInitialized);
    btnSetAll0->setEnabled(enable && isDeviceInitialized);
}

// ============================================================================
// FILE MENU SLOTS
// ============================================================================

void MainWindow::onNewProjectWizard()
{
    if (isDeviceDetected && scanController) {
        uint32_t idcode = scanController->getIDCODE();

        NewProjectWizard wizard(idcode, this);
        if (wizard.exec() == QDialog::Accepted) {
            auto packageType = wizard.getPackageType();

            if (packageType == PackageTypePage::PackageType::EDGE_PINS) {
                chipVisualizer->setPackageType("EDGE");
            } else {
                chipVisualizer->setPackageType("CENTER");
            }

            updateStatusBar("Project settings updated");
        }
    } else {
        QMessageBox::information(this, "New Project Wizard",
            "Please detect a device first (Scan > Examine Chain)");
    }
}

void MainWindow::onOpen()
{
    QString fileName = QFileDialog::getOpenFileName(this,
        tr("Open Project"), "", tr("Project Files (*.jtag *.prj);;All Files (*)"));
    
    if (!fileName.isEmpty()) {
        updateWindowTitle(fileName);
        updateStatusBar("Project opened: " + fileName);
    }
}

void MainWindow::onSave()
{
    updateStatusBar("Project saved");
}

void MainWindow::onSaveAs()
{
    QString fileName = QFileDialog::getSaveFileName(this,
        tr("Save Project As"), "", tr("Project Files (*.jtag *.prj);;All Files (*)"));
    
    if (!fileName.isEmpty()) {
        updateWindowTitle(fileName);
        updateStatusBar("Project saved as: " + fileName);
    }
}

void MainWindow::onExit()
{
    close();
}

// ============================================================================
// VIEW MENU SLOTS
// ============================================================================

void MainWindow::onTogglePins(bool checked)
{
    ui->dockPins->setVisible(checked);
}

void MainWindow::onToggleWatch(bool checked)
{
    ui->dockWatch->setVisible(checked);
}

void MainWindow::onToggleWaveform(bool checked)
{
    ui->dockWaveform->setVisible(checked);
}

void MainWindow::onZoom()
{
    QString zoomText = zoomComboBox->currentText();
    zoomText.remove('%');
    currentZoom = zoomText.toDouble() / 100.0;
    
    ui->graphicsView->resetTransform();
    ui->graphicsView->scale(currentZoom, currentZoom);
    
    updateStatusBar(QString("Zoom: %1%").arg(zoomText));
}

void MainWindow::onInoutPinsDisplaying(bool isIN)
{
    QString mode = isIN ? "IN of inout" : "OUT of inout";
    ui->labelWaveformStatus->setText(mode);
    updateStatusBar(QString("Inout pins displaying: %1").arg(mode));
}

// ============================================================================
// SCAN MENU SLOTS
// ============================================================================

void MainWindow::onJTAGConnection()
{
    if (!scanController) {
        QMessageBox::critical(this, "Error", "ScanController not initialized");
        return;
    }

    // 1. Detectar adaptadores disponibles REALMENTE
    auto adapters = scanController->getDetectedAdapters();

    if (adapters.empty()) {
        QMessageBox::warning(this, "No Adapters",
            "No JTAG adapters detected.\n\n"
            "Please ensure:\n"
            "- J-Link DLL is installed (for J-Link)\n"
            "- Pico is connected via USB (for Pico)");
        return;
    }

    // 2. Mostrar diálogo MEJORADO con lista + frecuencia
    ConnectionDialog dialog(adapters, this);

    if (dialog.exec() == QDialog::Accepted) {
        JTAG::AdapterType selectedType = dialog.getSelectedAdapter();
        uint32_t clockSpeed = dialog.getSelectedClockSpeed();

        // 3. Conectar (SIN auto-detección)
        if (scanController->connectAdapter(selectedType, clockSpeed)) {
            isAdapterConnected = true;

            // Encontrar nombre del adaptador
            QString adapterName;
            for (const auto& adapter : adapters) {
                if (adapter.type == selectedType) {
                    adapterName = QString::fromStdString(adapter.name);
                    break;
                }
            }

            updateStatusBar(QString("Connected to %1 @ %2 Hz")
                .arg(adapterName)
                .arg(clockSpeed));

            enableControlsAfterConnection(true);
        } else {
            // Mensaje de error detallado según el tipo de adaptador
            QString errorMsg = "Failed to connect to adapter.\n\n";

            switch (selectedType) {
                case JTAG::AdapterType::JLINK:
                    errorMsg += "J-Link troubleshooting:\n"
                               "• Check J-Link is connected via USB\n"
                               "• Verify drivers are installed\n"
                               "• Close other software using J-Link\n"
                               "• Try reconnecting the device";
                    break;

                case JTAG::AdapterType::PICO:
                    errorMsg += "Raspberry Pi Pico troubleshooting:\n"
                               "• Check Pico is connected via USB\n"
                               "• Verify correct firmware is loaded\n"
                               "• Check COM port is not in use\n"
                               "• Try reconnecting the device";
                    break;

                case JTAG::AdapterType::MOCK:
                    errorMsg += "Mock Adapter should always connect.\n"
                               "This is an unexpected error.";
                    break;

                default:
                    errorMsg += "Check adapter connection and try again.";
                    break;
            }

            QMessageBox::critical(this, "Connection Error", errorMsg);
        }
    }
}

void MainWindow::onExamineChain()
{
    if (!scanController || !isAdapterConnected) {
        QMessageBox::warning(this, "Not Connected",
            "Please connect to a JTAG adapter first (Scan > JTAG Connection)");
        return;
    }

    uint32_t idcode = scanController->detectDevice();

    if (idcode != 0 && idcode != 0xFFFFFFFF) {
        isDeviceDetected = true;

        // Mostrar diálogo (NO auto-cargar BSDL)
        ChainExamineDialog dialog(idcode, this);
        dialog.exec();

        // Actualizar combo
        ui->comboBoxDevice->clear();
        ui->comboBoxDevice->addItem(
            QString("Device 0x%1").arg(idcode, 8, 16, QChar('0')));

        updateStatusBar(QString("Device detected - IDCODE: 0x%1 (BSDL not loaded)")
            .arg(idcode, 8, 16, QChar('0')));

        // LANZAR New Project Wizard
        onNewProjectWizard();

    } else {
        QMessageBox::warning(this, "No Device",
            "No device detected on JTAG chain.\n\nCheck connections.");
    }
}

void MainWindow::onRun()
{
    if (!isDeviceInitialized) {
        QMessageBox::warning(this, "Not Ready", "Please initialize device first");
        return;
    }

    if (!isCapturing) {
        // Entrar en modo SAMPLE para capturar pines (el worker lo maneja)
        if (scanController->enterSAMPLE()) {
            isCapturing = true;
            captureTimer.start();  // Start waveform timestamp tracking
            scanController->startPolling();  // Iniciar worker thread
            updateStatusBar("Running - capturing pin states");
            ui->actionRun->setText("Stop");
        }
    } else {
        // Detener captura
        isCapturing = false;
        scanController->stopPolling();  // Detener worker thread
        updateStatusBar("Stopped");
        ui->actionRun->setText("Run");
    }
}

void MainWindow::onJTAGReset()
{
    if (scanController && isAdapterConnected) {
        if (scanController->reset()) {
            updateStatusBar("JTAG TAP reset successful");
        } else {
            QMessageBox::critical(this, "Error", "JTAG reset failed");
        }
    }
}

void MainWindow::onDeviceInstruction()
{
    // Diálogo para seleccionar instrucción (SAMPLE, EXTEST, BYPASS, etc.)
    QMessageBox::information(this, "Device Instruction", "Device Instruction dialog - To be implemented");
}

void MainWindow::onDeviceBSDLFile()
{
    if (!isAdapterConnected) {
        QMessageBox::warning(this, "Not Connected",
            "Please connect to JTAG adapter first");
        return;
    }

    QString fileName = QFileDialog::getOpenFileName(this,
        tr("Open BSDL File"), "", tr("BSDL Files (*.bsd *.bsdl);;All Files (*)"));

    if (!fileName.isEmpty() && scanController) {
        if (scanController->loadBSDL(fileName.toStdString())) {
            updateStatusBar("BSDL loaded: " + fileName);

            if (scanController->initializeDevice()) {
                isDeviceInitialized = true;

                updatePinsTable();
                renderChipVisualization();

                // NUEVO: Auto-entrar en SAMPLE y empezar polling
                if (scanController->enterSAMPLE()) {
                    isCapturing = true;
                    captureTimer.start();
                    scanController->startPolling();
                    updateStatusBar("SAMPLE mode active - reading pins continuously");
                    ui->actionRun->setText("Stop");
                }

                enableControlsAfterConnection(true);
            }
        } else {
            QMessageBox::critical(this, "Error",
                "Failed to load or parse BSDL file");
        }
    }
}

void MainWindow::onDevicePackage()
{
    if (!scanController || !isDeviceDetected) {
        QMessageBox::warning(this, "Device Package",
                           "No device detected or BSDL not loaded");
        return;
    }

    QString info;
    info += "Device: " + QString::fromStdString(scanController->getDeviceName()) + "\n";
    info += "IDCODE: 0x" + QString::number(scanController->getIDCODE(), 16).toUpper().rightJustified(8, '0') + "\n";
    info += "Package: " + QString::fromStdString(scanController->getPackageInfo()) + "\n";
    info += "\nBoundary Scan Chain:\n";

    // Obtener valores reales del DeviceModel (NO hardcodeados)
    const JTAG::DeviceModel* deviceModel = scanController->getDeviceModel();
    size_t irLength = deviceModel ? deviceModel->getIRLength() : 0;
    size_t bsrLength = deviceModel ? deviceModel->getBSRLength() : 0;

    info += "  IR Length: " + QString::number(irLength) + " bits\n";
    info += "  BSR Length: " + QString::number(bsrLength) + " bits\n";
    info += "  Pin Count: " + QString::number(scanController->getPinList().size()) + "\n";

    QMessageBox::information(this, "Device Package Information", info);
}

void MainWindow::onDeviceProperties()
{
    if (!scanController || !isDeviceDetected) {
        QMessageBox::warning(this, "No Device", "No device detected");
        return;
    }

    QString info;
    info += "Device Name: " + QString::fromStdString(scanController->getDeviceName()) + "\n";
    info += "IDCODE: 0x" + QString::number(scanController->getIDCODE(), 16).toUpper() + "\n";
    info += "Adapter: " + QString::fromStdString(scanController->getAdapterInfo()) + "\n";

    QMessageBox::information(this, "Device Properties", info);
}

// ============================================================================
// PINS MENU SLOTS
// ============================================================================

void MainWindow::onSearchPins()
{
    ui->lineEditSearchPins->setFocus();
    ui->lineEditSearchPins->selectAll();
}

void MainWindow::onSearchPinsButton()
{
    QString searchText = ui->lineEditSearchPins->text();
    
    if (searchText.isEmpty()) {
        // Clear search - show all pins
        for (int i = 0; i < ui->tableWidgetPins->rowCount(); i++) {
            ui->tableWidgetPins->setRowHidden(i, false);
        }
        updateStatusBar("Search cleared");
        return;
    }
    
    // Search and hide non-matching rows
    int visibleCount = 0;
    for (int i = 0; i < ui->tableWidgetPins->rowCount(); i++) {
        QTableWidgetItem *nameItem = ui->tableWidgetPins->item(i, 0);
        if (nameItem) {
            bool matches = nameItem->text().contains(searchText, Qt::CaseInsensitive);
            ui->tableWidgetPins->setRowHidden(i, !matches);
            if (matches) visibleCount++;
        }
    }
    
    updateStatusBar(QString("Found %1 pin(s) matching '%2'").arg(visibleCount).arg(searchText));
}

void MainWindow::onPinTableItemChanged(QTableWidgetItem* item)
{
    // Handle column 0 (Name) changes
    if (item->column() == 0) {
        QString newDisplayName = item->text();
        QString realName = item->data(Qt::UserRole).toString();

        qDebug() << "[onPinTableItemChanged] Pin display name changed to:" << newDisplayName
                 << "(real name:" << realName << ")";

        // El cambio de nombre ya está hecho en el item
        // resolveRealPinName() usará el UserRole automáticamente
        return;
    }

    // Handle column 3 (I/O Value) changes in EXTEST mode
    if (item->column() == 3 && currentJTAGMode == JTAGMode::EXTEST) {
        if (!scanController) return;

        // Get the pin name from column 0
        int row = item->row();
        QTableWidgetItem* nameItem = ui->tableWidgetPins->item(row, 0);
        if (!nameItem) return;

        QString displayName = nameItem->text();
        QString realName = resolveRealPinName(displayName);
        std::string pinName = realName.toStdString();

        // Parse the new value
        QString valueStr = item->text().toUpper();
        JTAG::PinLevel newLevel;

        if (valueStr == "0") {
            newLevel = JTAG::PinLevel::LOW;
        } else if (valueStr == "1") {
            newLevel = JTAG::PinLevel::HIGH;
        } else if (valueStr == "Z") {
            newLevel = JTAG::PinLevel::HIGH_Z;
        } else {
            // Invalid value - restore previous
            updatePinsTable();
            return;
        }

        // Apply the change
        if (scanController->setPin(pinName, newLevel)) {
            scanController->applyChanges();
            qDebug() << "[onPinTableItemChanged] Set pin" << realName << "to" << valueStr;
            updateStatusBar(QString("Set %1 to %2").arg(realName).arg(valueStr));
        } else {
            QMessageBox::warning(this, "Pin Update Failed",
                QString("Could not set pin %1 to %2").arg(realName).arg(valueStr));
            updatePinsTable(); // Restore table
        }
    }
}

void MainWindow::onPinTableSelectionChanged()
{
    // Obtener fila seleccionada
    QList<QTableWidgetItem*> selected = ui->tableWidgetPins->selectedItems();
    if (selected.isEmpty()) {
        chipVisualizer->clearHighlight();
        return;
    }

    // Nombre del pin está en columna 0
    int row = selected.first()->row();
    QTableWidgetItem* nameItem = ui->tableWidgetPins->item(row, 0);
    if (!nameItem) return;

    // Usar el nombre REAL para el visualizador (guardado en UserRole)
    QString realPinName = nameItem->data(Qt::UserRole).toString();
    if (realPinName.isEmpty()) {
        realPinName = nameItem->text(); // Fallback al nombre de display
    }

    chipVisualizer->highlightPin(realPinName);
}

void MainWindow::onEditPinNamesAndBuses()
{
    QMessageBox::information(this, "Edit Pin Names", "Edit Pin Names and Buses - To be implemented");
}

void MainWindow::onSetTo0()
{
    QList<QTableWidgetItem*> selectedItems = ui->tableWidgetPins->selectedItems();
    if (selectedItems.isEmpty()) {
        updateStatusBar("No pins selected");
        return;
    }

    // Get unique rows
    QSet<int> rows;
    for (auto item : selectedItems) {
        rows.insert(item->row());
    }

    for (int row : rows) {
        QTableWidgetItem *nameItem = ui->tableWidgetPins->item(row, 0);
        if (nameItem) {
            std::string pinName = nameItem->text().toStdString();
            scanController->setPinAsync(pinName, JTAG::PinLevel::LOW);
        }
    }

    // No se necesita applyChanges() - el worker lo hace automáticamente
    updateStatusBar(QString("Set %1 pin(s) to 0").arg(rows.size()));
}

void MainWindow::onSetTo1()
{
    // Similar a onSetTo0() pero con PinLevel::HIGH
    QList<QTableWidgetItem*> selectedItems = ui->tableWidgetPins->selectedItems();
    if (selectedItems.isEmpty()) {
        updateStatusBar("No pins selected");
        return;
    }

    QSet<int> rows;
    for (auto item : selectedItems) {
        rows.insert(item->row());
    }

    for (int row : rows) {
        QTableWidgetItem *nameItem = ui->tableWidgetPins->item(row, 0);
        if (nameItem) {
            std::string pinName = nameItem->text().toStdString();
            scanController->setPinAsync(pinName, JTAG::PinLevel::HIGH);
        }
    }

    // No se necesita applyChanges() - el worker lo hace automáticamente
    updateStatusBar(QString("Set %1 pin(s) to 1").arg(rows.size()));
}

void MainWindow::onSetToZ()
{
    // Similar a onSetTo0() pero con PinLevel::HIGH_Z
    QList<QTableWidgetItem*> selectedItems = ui->tableWidgetPins->selectedItems();
    if (selectedItems.isEmpty()) {
        updateStatusBar("No pins selected");
        return;
    }

    QSet<int> rows;
    for (auto item : selectedItems) {
        rows.insert(item->row());
    }

    for (int row : rows) {
        QTableWidgetItem *nameItem = ui->tableWidgetPins->item(row, 0);
        if (nameItem) {
            std::string pinName = nameItem->text().toStdString();
            scanController->setPinAsync(pinName, JTAG::PinLevel::HIGH_Z);
        }
    }

    // No se necesita applyChanges() - el worker lo hace automáticamente
    updateStatusBar(QString("Set %1 pin(s) to Z").arg(rows.size()));
}

void MainWindow::onTogglePinValue()
{
    QList<QTableWidgetItem*> selectedItems = ui->tableWidgetPins->selectedItems();
    if (selectedItems.isEmpty()) {
        updateStatusBar("No pins selected");
        return;
    }

    QSet<int> rows;
    for (auto item : selectedItems) {
        rows.insert(item->row());
    }

    for (int row : rows) {
        QTableWidgetItem *nameItem = ui->tableWidgetPins->item(row, 0);
        if (nameItem) {
            std::string pinName = nameItem->text().toStdString();
            auto currentLevel = scanController->getPin(pinName);

            if (currentLevel.has_value()) {
                // Toggle: 0→1, 1→0, Z→1
                JTAG::PinLevel newLevel;
                if (currentLevel.value() == JTAG::PinLevel::LOW) {
                    newLevel = JTAG::PinLevel::HIGH;
                } else {
                    newLevel = JTAG::PinLevel::LOW;
                }
                scanController->setPinAsync(pinName, newLevel);
            }
        }
    }

    // No se necesita applyChanges() - el worker lo hace automáticamente
    updateStatusBar(QString("Toggled %1 pin(s)").arg(rows.size()));
}

void MainWindow::onSetBusValue()
{
    QList<QTableWidgetItem*> selectedItems = ui->tableWidgetPins->selectedItems();
    if (selectedItems.isEmpty()) {
        updateStatusBar("No pins selected");
        return;
    }

    // Obtener nombres de pines seleccionados
    QSet<int> rows;
    for (auto item : selectedItems) {
        rows.insert(item->row());
    }

    std::vector<std::string> pinNames;
    for (int row : rows) {
        QTableWidgetItem *nameItem = ui->tableWidgetPins->item(row, 0);
        if (nameItem) {
            pinNames.push_back(nameItem->text().toStdString());
        }
    }

    if (pinNames.empty()) return;

    // Pedir valor al usuario
    bool ok;
    QString valueStr = QInputDialog::getText(this, "Set Bus Value",
        QString("Enter value for %1-bit bus (hex):").arg(pinNames.size()),
        QLineEdit::Normal, "0", &ok);

    if (ok && !valueStr.isEmpty()) {
        // Convertir hex a uint32_t
        uint32_t value = valueStr.toUInt(&ok, 16);
        if (ok) {
            if (scanController->writeBus(pinNames, value)) {
                scanController->applyChanges();
                updateStatusBar(QString("Bus value set to 0x%1").arg(value, 0, 16));
            } else {
                QMessageBox::critical(this, "Error", "Failed to set bus value");
            }
        }
    }
}

void MainWindow::onSetBusToAllZ()
{
    QList<QTableWidgetItem*> selectedItems = ui->tableWidgetPins->selectedItems();
    if (selectedItems.isEmpty()) {
        updateStatusBar("No pins selected");
        return;
    }

    QSet<int> rows;
    for (auto item : selectedItems) {
        rows.insert(item->row());
    }

    for (int row : rows) {
        QTableWidgetItem *nameItem = ui->tableWidgetPins->item(row, 0);
        if (nameItem) {
            scanController->setPin(nameItem->text().toStdString(), JTAG::PinLevel::HIGH_Z);
        }
    }

    scanController->applyChanges();
    updateStatusBar(QString("Set %1 pin(s) to High-Z").arg(rows.size()));
}

void MainWindow::onSetAllDevicePinsToBSDLSafe()
{
    if (!scanController || !isDeviceInitialized) {
        QMessageBox::warning(this, "Not Ready", "Device not initialized");
        return;
    }

    // Default safe value: HIGH_Z (tristate)
    auto pinNames = scanController->getPinList();

    for (const auto& pinName : pinNames) {
        scanController->setPin(pinName, JTAG::PinLevel::HIGH_Z);
    }

    if (scanController->applyChanges()) {
        updateStatusBar(QString("Set %1 pins to safe state (HIGH_Z)").arg(pinNames.size()));
    }
}

// ============================================================================
// WATCH MENU SLOTS
// ============================================================================

void MainWindow::onWatchShow()
{
    ui->dockWatch->setVisible(true);
    ui->actionWatch->setChecked(true);
}

void MainWindow::onWatchAddSignal()
{
    QList<QTableWidgetItem*> selectedItems = ui->tableWidgetPins->selectedItems();
    if (selectedItems.isEmpty()) {
        updateStatusBar("No pins selected");
        return;
    }

    QSet<int> rows;
    for (auto item : selectedItems) {
        rows.insert(item->row());
    }

    for (int row : rows) {
        // Copiar fila de Pins a Watch
        int watchRow = ui->tableWidgetWatch->rowCount();
        ui->tableWidgetWatch->insertRow(watchRow);

        // Copiar columnas 0-4 de Pins (Name, Pin#, Port, I/O Value, Type)
        for (int col = 0; col < 5; col++) {
            QTableWidgetItem *sourceItem = ui->tableWidgetPins->item(row, col);
            if (col < 4) {
                // Columnas 0-3: Name, Pin#, Port, I/O Value
                if (sourceItem) {
                    ui->tableWidgetWatch->setItem(watchRow, col,
                        new QTableWidgetItem(sourceItem->text()));
                }
            } else {
                // Columna 4 en Watch es "Transitions Count", inicializar a 0
                ui->tableWidgetWatch->setItem(watchRow, 4, new QTableWidgetItem("0"));
                // Columna 5 en Watch es "Type", copiar de columna 4 de Pins
                if (sourceItem) {
                    ui->tableWidgetWatch->setItem(watchRow, 5,
                        new QTableWidgetItem(sourceItem->text()));
                }
            }
        }
    }

    updateStatusBar(QString("Added %1 signal(s) to Watch").arg(rows.size()));
}

void MainWindow::onWatchRemove()
{
    QList<QTableWidgetItem*> selectedItems = ui->tableWidgetWatch->selectedItems();
    if (selectedItems.isEmpty()) {
        updateStatusBar("No signals selected in Watch");
        return;
    }
    
    QSet<int> rows;
    for (auto item : selectedItems) {
        rows.insert(item->row());
    }
    
    // Remove in reverse order to avoid index issues
    QList<int> rowList = rows.values();
    std::sort(rowList.begin(), rowList.end(), std::greater<int>());
    
    for (int row : rowList) {
        ui->tableWidgetWatch->removeRow(row);
    }
    
    updateStatusBar(QString("Removed %1 signal(s) from Watch").arg(rows.size()));
}

void MainWindow::onWatchRemoveAll()
{
    ui->tableWidgetWatch->setRowCount(0);
    updateStatusBar("Watch cleared");
}

void MainWindow::onWatchZeroTransitionCounter()
{
    QList<QTableWidgetItem*> selectedItems = ui->tableWidgetWatch->selectedItems();
    if (selectedItems.isEmpty()) {
        updateStatusBar("No signals selected");
        return;
    }

    QSet<int> rows;
    for (auto item : selectedItems) {
        rows.insert(item->row());
    }

    for (int row : rows) {
        QTableWidgetItem *nameItem = ui->tableWidgetWatch->item(row, 0);
        if (nameItem) {
            std::string pinName = nameItem->text().toStdString();
            transitionCounters[pinName] = 0;
        }
    }

    updateStatusBar(QString("Zeroed %1 counter(s)").arg(rows.size()));
}

void MainWindow::onWatchZeroAllTransitionCounters()
{
    transitionCounters.clear();
    updateStatusBar("All transition counters zeroed");
}

// ============================================================================
// WAVEFORM MENU SLOTS
// ============================================================================

void MainWindow::onWaveformClose()
{
    ui->dockWaveform->setVisible(false);
    ui->actionWaveform->setChecked(false);
}

void MainWindow::onWaveformAddSignal()
{
    QList<QTableWidgetItem*> selectedItems = ui->tableWidgetPins->selectedItems();
    if (selectedItems.isEmpty()) {
        updateStatusBar("No pins selected");
        return;
    }

    QSet<int> rows;
    for (auto item : selectedItems) {
        rows.insert(item->row());
    }

    for (int row : rows) {
        int waveRow = ui->tableWidgetWaveform->rowCount();
        ui->tableWidgetWaveform->insertRow(waveRow);

        // Copy first column (Name) only - waveform doesn't need all columns
        QTableWidgetItem *sourceItem = ui->tableWidgetPins->item(row, 0);
        if (sourceItem) {
            ui->tableWidgetWaveform->setItem(waveRow, 0,
                new QTableWidgetItem(sourceItem->text()));

            // Initialize waveform buffer for this signal
            std::string pinName = sourceItem->text().toStdString();
            waveformBuffer[pinName].clear();
        }
    }

    updateStatusBar(QString("Added %1 signal(s) to Waveform").arg(rows.size()));
}

void MainWindow::onWaveformRemove()
{
    QList<QTableWidgetItem*> selectedItems = ui->tableWidgetWaveform->selectedItems();
    if (selectedItems.isEmpty()) {
        updateStatusBar("No signals selected");
        return;
    }

    QSet<int> rows;
    for (auto item : selectedItems) {
        rows.insert(item->row());
    }

    // Get pin names before removing rows
    std::vector<std::string> removedPins;
    for (int row : rows) {
        QTableWidgetItem *nameItem = ui->tableWidgetWaveform->item(row, 0);
        if (nameItem) {
            removedPins.push_back(nameItem->text().toStdString());
        }
    }

    // Remove rows in reverse order
    QList<int> rowList = rows.values();
    std::sort(rowList.begin(), rowList.end(), std::greater<int>());
    for (int row : rowList) {
        ui->tableWidgetWaveform->removeRow(row);
    }

    // Clear waveform buffers
    for (const auto& pin : removedPins) {
        waveformBuffer.erase(pin);
    }

    updateStatusBar(QString("Removed %1 signal(s)").arg(rows.size()));
}

void MainWindow::onWaveformRemoveAll()
{
    ui->tableWidgetWaveform->setRowCount(0);
    updateStatusBar("Waveform signals cleared");
}

void MainWindow::onWaveformClear()
{
    // Clear waveform data but keep signals
    waveformScene->clear();
    
    // Recreate grid
    QPen gridPen(QColor(220, 220, 220));
    for (int i = 0; i < 20; i++) {
        waveformScene->addLine(i * 50, 0, i * 50, 200, gridPen);
    }
    
    updateStatusBar("Waveform data cleared");
}

void MainWindow::onWaveformZoom()
{
    bool ok;
    double newTimebase = QInputDialog::getDouble(this, "Waveform Zoom",
                                                 "Timebase (seconds):", 
                                                 waveformTimebase, 0.001, 100.0, 3, &ok);
    if (ok) {
        waveformTimebase = newTimebase;
        updateStatusBar(QString("Waveform timebase: %1 s").arg(waveformTimebase));
    }
}

void MainWindow::onWaveformZoomIn()
{
    waveformTimebase /= 2.0;
    updateStatusBar(QString("Waveform zoom in: %1 s").arg(waveformTimebase));
}

void MainWindow::onWaveformZoomOut()
{
    waveformTimebase *= 2.0;
    updateStatusBar(QString("Waveform zoom out: %1 s").arg(waveformTimebase));
}

void MainWindow::onWaveformGoToTime()
{
    bool ok;
    double targetTime = QInputDialog::getDouble(this, "Go to Time",
        "Enter time (seconds):", 0.0, 0.0, 1000.0, 3, &ok);

    if (ok) {
        // Scroll waveform view to target time
        double pixelX = targetTime * (100.0 / waveformTimebase);
        ui->graphicsViewWaveform->centerOn(pixelX, 0);
        updateStatusBar(QString("Jumped to time %1 s").arg(targetTime));
    }
}

void MainWindow::onWaveformPreviousEvent()
{
    // Find previous transition in any signal
    double currentViewTime = ui->graphicsViewWaveform->mapToScene(
        ui->graphicsViewWaveform->viewport()->rect().center()).x() / (100.0 / waveformTimebase);

    double closestTime = -1.0;

    for (const auto& [pinName, samples] : waveformBuffer) {
        for (size_t i = 1; i < samples.size(); i++) {
            if (samples[i].level != samples[i-1].level &&
                samples[i].timestamp < currentViewTime) {
                if (closestTime < 0 || samples[i].timestamp > closestTime) {
                    closestTime = samples[i].timestamp;
                }
            }
        }
    }

    if (closestTime >= 0) {
        double pixelX = closestTime * (100.0 / waveformTimebase);
        ui->graphicsViewWaveform->centerOn(pixelX, 0);
        updateStatusBar(QString("Previous event at %1 s").arg(closestTime));
    } else {
        updateStatusBar("No previous event found");
    }
}

void MainWindow::onWaveformNextEvent()
{
    // Similar to previous but search forward
    double currentViewTime = ui->graphicsViewWaveform->mapToScene(
        ui->graphicsViewWaveform->viewport()->rect().center()).x() / (100.0 / waveformTimebase);

    double closestTime = -1.0;

    for (const auto& [pinName, samples] : waveformBuffer) {
        for (size_t i = 1; i < samples.size(); i++) {
            if (samples[i].level != samples[i-1].level &&
                samples[i].timestamp > currentViewTime) {
                if (closestTime < 0 || samples[i].timestamp < closestTime) {
                    closestTime = samples[i].timestamp;
                }
            }
        }
    }

    if (closestTime >= 0) {
        double pixelX = closestTime * (100.0 / waveformTimebase);
        ui->graphicsViewWaveform->centerOn(pixelX, 0);
        updateStatusBar(QString("Next event at %1 s").arg(closestTime));
    } else {
        updateStatusBar("No next event found");
    }
}

// ============================================================================
// HELP MENU SLOTS
// ============================================================================

void MainWindow::onHelpContents()
{
    QMessageBox::information(this, "Help", 
        "This is a JTAG Boundary Scan tool.\n\n"
        "Basic workflow:\n"
        "1. Connect to JTAG adapter (Scan > JTAG Connection)\n"
        "2. Detect device (Scan > Examine the Chain)\n"
        "3. Load BSDL file (Scan > Device BSDL File)\n"
        "4. Run to capture pin states (Scan > Run or F5)\n"
        "5. Control pins via Pins panel");
}

void MainWindow::onTurnOnLogging()
{
    updateStatusBar("Logging feature - To be implemented");
}

void MainWindow::onRegister()
{
    updateStatusBar("Registration - Not applicable");
}

void MainWindow::onAbout()
{
    QMessageBox::about(this, "About",
        "JTAG Boundary Scan Tool\n"
        "Version 1.0\n\n"
        "Based on TopJTAG design\n"
        "Built with Qt 6.7.3");
}

// ============================================================================
// TOOLBAR SLOTS
// ============================================================================

void MainWindow::onInstruction()
{
    onDeviceInstruction();
}

// ============================================================================
// PINS PANEL SLOTS
// ============================================================================

void MainWindow::onDeviceChanged(int index)
{
    if (index >= 0) {
        updateStatusBar(QString("Device selected: index %1").arg(index));
    }
}

// ============================================================================
// WAVEFORM TOOLBAR SLOTS
// ============================================================================

void MainWindow::onWaveZoomIn()
{
    onWaveformZoomIn();
}

void MainWindow::onWaveZoomOut()
{
    onWaveformZoomOut();
}

void MainWindow::onWaveFit()
{
    updateStatusBar("Waveform fit to window");
}

void MainWindow::onWavePrev()
{
    updateStatusBar("Waveform previous");
}

void MainWindow::onWaveNext()
{
    updateStatusBar("Waveform next");
}

void MainWindow::onWaveGoto()
{
    onWaveformGoToTime();
}

// ============================================================================
// POLLING AND BACKEND INTEGRATION
// ============================================================================
// NOTA: onPollTimer() ELIMINADO - El polling ahora lo maneja ScanWorker en thread separado
//       Las actualizaciones de UI se hacen en onPinsDataReady() que recibe señales del worker
// ============================================================================

void MainWindow::updatePinsTable()
{
    // ==================== PUNTO DE INTEGRACIÓN 11 ====================
    if (!scanController) return;

    // Obtener lista de pines del modelo
    std::vector<std::string> pinNames = scanController->getPinList();
    qDebug() << "[MainWindow::updatePinsTable] Updating" << pinNames.size() << "pins";

    // NOTA: NO limpiar la tabla si ya tiene filas con nombres editados
    // Solo limpiar si es la primera carga (tabla vacía)
    bool isFirstLoad = (ui->tableWidgetPins->rowCount() == 0);

    if (isFirstLoad) {
        ui->tableWidgetPins->setRowCount(0);

        // Llenar tabla con información completa
        for (const auto& pinName : pinNames) {
            int row = ui->tableWidgetPins->rowCount();
            ui->tableWidgetPins->insertRow(row);

            QString qPinName = QString::fromStdString(pinName);

            // Columna 0: Name (editable por el usuario)
            QTableWidgetItem* nameItem = new QTableWidgetItem(qPinName);
            // Guardar el nombre REAL del pin en UserRole (no visible, no editable)
            nameItem->setData(Qt::UserRole, qPinName);
            ui->tableWidgetPins->setItem(row, 0, nameItem);

            // Columna 1: Pin #
            QString pinNumStr = QString::fromStdString(scanController->getPinNumber(pinName));
            ui->tableWidgetPins->setItem(row, 1, new QTableWidgetItem(pinNumStr));

            // Columna 2: Port
            QString port = QString::fromStdString(scanController->getPinPort(pinName));
            ui->tableWidgetPins->setItem(row, 2, new QTableWidgetItem(port));

            // Columna 3: I/O Value (se actualizará con polling)
            ui->tableWidgetPins->setItem(row, 3, new QTableWidgetItem("?"));

            // Columna 4: Type
            QString type = QString::fromStdString(scanController->getPinType(pinName));
            ui->tableWidgetPins->setItem(row, 4, new QTableWidgetItem(type));
        }
    }

    // Actualizar valores I/O en tabla Y en visualizador
    for (int row = 0; row < ui->tableWidgetPins->rowCount(); row++) {
        QTableWidgetItem *nameItem = ui->tableWidgetPins->item(row, 0);
        if (nameItem) {
            QString displayName = nameItem->text();
            QString realName = resolveRealPinName(displayName);
            std::string pinName = realName.toStdString();

            auto level = scanController->getPin(pinName);

            if (level.has_value()) {
                QString valueStr;
                VisualPinState visualState;

                switch (level.value()) {
                    case JTAG::PinLevel::LOW:
                        valueStr = "0";
                        visualState = VisualPinState::LOW;
                        break;
                    case JTAG::PinLevel::HIGH:
                        valueStr = "1";
                        visualState = VisualPinState::HIGH;
                        break;
                    case JTAG::PinLevel::HIGH_Z:
                        valueStr = "Z";
                        visualState = VisualPinState::UNKNOWN;
                        break;
                }

                // Actualizar tabla
                QTableWidgetItem *valueItem = ui->tableWidgetPins->item(row, 3);
                if (valueItem) {
                    valueItem->setText(valueStr);

                    // Make cell editable in EXTEST mode for OUTPUT/INOUT pins
                    QString type = QString::fromStdString(scanController->getPinType(pinName));
                    bool isEditable = (currentJTAGMode == JTAGMode::EXTEST) &&
                                     (type == "OUTPUT" || type == "INOUT");

                    if (isEditable) {
                        valueItem->setFlags(valueItem->flags() | Qt::ItemIsEditable);
                        valueItem->setBackground(QColor(255, 255, 200)); // Light yellow
                    } else {
                        valueItem->setFlags(valueItem->flags() & ~Qt::ItemIsEditable);
                        valueItem->setBackground(Qt::white);
                    }
                }

                // Actualizar visualizador del chip (usar nombre real para el mapa interno)
                if (chipVisualizer) {
                    chipVisualizer->updatePinState(realName, visualState);
                }

                qDebug() << "[updatePinsTable] Pin" << QString::fromStdString(pinName)
                    << "= " << valueStr << "-> visualState:" << (int)visualState;

            } else {
                // DEBUG: Si no tiene valor, mostrar
                if (row == 0) {
                    qDebug() << "[updatePinsTable] Pin" << QString::fromStdString(pinName)
                             << "has NO VALUE (std::nullopt)";
                }
            }
        }
    }
    // ================================================================
}

QString MainWindow::resolveRealPinName(const QString& displayName) const
{
    // Buscar en la tabla el item con este displayName
    // y obtener el nombre real desde UserRole
    for (int row = 0; row < ui->tableWidgetPins->rowCount(); row++) {
        QTableWidgetItem* nameItem = ui->tableWidgetPins->item(row, 0);
        if (nameItem && nameItem->text() == displayName) {
            // El nombre real está guardado en UserRole
            QString realName = nameItem->data(Qt::UserRole).toString();
            return realName.isEmpty() ? displayName : realName;
        }
    }
    // Si no se encuentra, asumir que displayName es el nombre real
    return displayName;
}

void MainWindow::renderChipVisualization()
{
    // Check if controller and device are initialized
    if (!scanController || !scanController->isInitialized()) {
        return;
    }

    // Get the DeviceModel from the controller
    const auto* deviceModel = scanController->getDeviceModel();
    if (!deviceModel) {
        return;
    }

    // Render the chip visualization using the device model
    chipVisualizer->renderFromDeviceModel(*deviceModel);
}

void MainWindow::updateWatchTable()
{
    // ==================== PUNTO DE INTEGRACIÓN 12 ====================
    if (!scanController) return;

    for (int row = 0; row < ui->tableWidgetWatch->rowCount(); row++) {
        QTableWidgetItem *nameItem = ui->tableWidgetWatch->item(row, 0);
        if (nameItem) {
            std::string pinName = nameItem->text().toStdString();
            auto level = scanController->getPin(pinName);

            if (level.has_value()) {
                // Update value
                QString valueStr;
                switch (level.value()) {
                    case JTAG::PinLevel::LOW: valueStr = "0"; break;
                    case JTAG::PinLevel::HIGH: valueStr = "1"; break;
                    case JTAG::PinLevel::HIGH_Z: valueStr = "Z"; break;
                }

                QTableWidgetItem *valueItem = ui->tableWidgetWatch->item(row, 3);
                if (valueItem) {
                    valueItem->setText(valueStr);
                }

                // Detect transition
                if (previousLevels.count(pinName) > 0 &&
                    previousLevels[pinName] != level.value()) {
                    transitionCounters[pinName]++;
                }
                previousLevels[pinName] = level.value();

                // Update transition counter (column 4)
                QTableWidgetItem *transItem = ui->tableWidgetWatch->item(row, 4);
                if (transItem) {
                    transItem->setText(QString::number(transitionCounters[pinName]));
                } else {
                    ui->tableWidgetWatch->setItem(row, 4,
                        new QTableWidgetItem(QString::number(transitionCounters[pinName])));
                }
            }
        }
    }
    // ================================================================
}

void MainWindow::captureWaveformSample()
{
    // ==================== PUNTO DE INTEGRACIÓN 13 ====================
    if (!scanController || waveformBuffer.empty()) return;

    double currentTime = captureTimer.elapsed() / 1000.0; // Convert ms to seconds

    // Capture sample for each signal in waveform
    for (int row = 0; row < ui->tableWidgetWaveform->rowCount(); row++) {
        QTableWidgetItem *nameItem = ui->tableWidgetWaveform->item(row, 0);
        if (nameItem) {
            std::string pinName = nameItem->text().toStdString();
            auto level = scanController->getPin(pinName);

            if (level.has_value()) {
                // Add sample to buffer
                waveformBuffer[pinName].push_back({currentTime, level.value()});

                // Maintain circular buffer
                if (waveformBuffer[pinName].size() > MAX_WAVEFORM_SAMPLES) {
                    waveformBuffer[pinName].pop_front();
                }
            }
        }
    }

    // Redraw waveform (throttle to avoid performance issues - every 5th sample)
    static int redrawCounter = 0;
    if (++redrawCounter >= 5) {
        redrawCounter = 0;
        redrawWaveform();
    }
    // ================================================================
}

void MainWindow::redrawWaveform()
{
    waveformScene->clear();

    const int SIGNAL_HEIGHT = 40;      // Vertical space per signal
    const int HIGH_Y_OFFSET = 10;      // Y offset for HIGH level
    const int LOW_Y_OFFSET = 30;       // Y offset for LOW level
    const double PIXELS_PER_SECOND = 100.0 / waveformTimebase; // Zoom factor

    // Draw grid
    QPen gridPen(QColor(220, 220, 220));
    int maxX = 2000; // Grid width
    int maxY = ui->tableWidgetWaveform->rowCount() * SIGNAL_HEIGHT;

    for (int x = 0; x < maxX; x += 50) {
        waveformScene->addLine(x, 0, x, maxY, gridPen);
    }

    // Draw each signal
    for (int row = 0; row < ui->tableWidgetWaveform->rowCount(); row++) {
        QTableWidgetItem *nameItem = ui->tableWidgetWaveform->item(row, 0);
        if (!nameItem) continue;

        std::string pinName = nameItem->text().toStdString();
        auto& samples = waveformBuffer[pinName];
        if (samples.empty()) continue;

        int yBase = row * SIGNAL_HEIGHT;
        int yHigh = yBase + HIGH_Y_OFFSET;
        int yLow = yBase + LOW_Y_OFFSET;

        // Draw signal name label
        QGraphicsTextItem *label = waveformScene->addText(QString::fromStdString(pinName));
        label->setPos(5, yBase + 5);
        label->setDefaultTextColor(Qt::black);

        // Draw waveform
        QPen signalPen(Qt::blue, 2);

        for (size_t i = 1; i < samples.size(); i++) {
            double x1 = samples[i-1].timestamp * PIXELS_PER_SECOND;
            double x2 = samples[i].timestamp * PIXELS_PER_SECOND;

            int y1 = (samples[i-1].level == JTAG::PinLevel::HIGH) ? yHigh : yLow;
            int y2 = (samples[i].level == JTAG::PinLevel::HIGH) ? yHigh : yLow;

            // Horizontal line (hold previous level)
            waveformScene->addLine(x1, y1, x2, y1, signalPen);

            // Vertical line (transition)
            if (y1 != y2) {
                waveformScene->addLine(x2, y1, x2, y2, signalPen);
            }
        }

        // Draw separator line
        waveformScene->addLine(0, yBase + SIGNAL_HEIGHT, maxX, yBase + SIGNAL_HEIGHT,
                              QPen(QColor(180, 180, 180)));
    }
}

// ============================================================================
// HELPER METHODS
// ============================================================================

void MainWindow::updateWindowTitle(const QString &filename)
{
    if (filename.isEmpty()) {
        setWindowTitle("Untitled - TopJTAG Probe");
    } else {
        QFileInfo fileInfo(filename);
        setWindowTitle(fileInfo.fileName() + " - TopJTAG Probe");
    }
}

void MainWindow::updateStatusBar(const QString &message)
{
    statusBar()->showMessage(message);
}

// ============================================================================
// NUEVOS SLOTS PARA THREADING (RECIBEN SEÑALES DEL SCANWORKER)
// ============================================================================

void MainWindow::onPinsDataReady(std::vector<JTAG::PinLevel> pins)
{
    // Este slot se ejecuta en GUI thread (thread-safe vía Qt signals)
    // Reemplaza el código que estaba en onPollTimer()

    qDebug() << "[MainWindow::onPinsDataReady] Called with" << pins.size() << "pins"
             << "isCapturing:" << isCapturing;

    // Check if no target is detected (all pull-ups)
    static bool warningShown = false;
    if (scanController && scanController->isNoTargetDetected()) {
        if (!warningShown) {
            statusBar()->showMessage("⚠ WARNING: No target detected - TDO showing pull-ups (all 1s)", 0);
            warningShown = true;
        }
    } else {
        if (warningShown) {
            warningShown = false;
            statusBar()->clearMessage();
        }
    }

    // Mostrar en barra de estado que recibimos datos (DEBUG)
    static int updateCount = 0;
    updateCount++;
    if (!warningShown) { // Only show update count if no warning
        statusBar()->showMessage(QString("Updates received: %1 (pins: %2)")
                                .arg(updateCount).arg(pins.size()), 100);
    }

    if (!scanController || !isCapturing) {
        qDebug() << "[MainWindow::onPinsDataReady] SKIPPED - not capturing";
        return;
    }

    // 1. Actualizar tabla de pines
    updatePinsTable();

    // 2. Actualizar tabla de watch con detección de transiciones
    updateWatchTable();

    // 3. Capturar muestra para waveform
    captureWaveformSample();
}

void MainWindow::onScanError(QString message)
{
    // Mostrar error en status bar
    statusBar()->showMessage("Scan error: " + message, 5000);

    // Opcional: detener captura automáticamente en caso de error
    if (isCapturing) {
        scanController->stopPolling();
        isCapturing = false;
        ui->actionRun->setText("Run");
        updateStatusBar("Stopped due to error");
    }
}

// ============================================================================
// JTAG MODE SELECTION AND QUICK ACTIONS
// ============================================================================

void MainWindow::onJTAGModeChanged(int modeId)
{
    if (!scanController) {
        QMessageBox::warning(this, "No Controller",
            "Scan controller not initialized");
        return;
    }

    bool success = false;
    QString modeName;

    switch (modeId) {
        case 0: // SAMPLE
            success = scanController->enterSAMPLE();
            modeName = "SAMPLE";
            currentJTAGMode = JTAGMode::SAMPLE;
            break;

        case 1: // EXTEST
            success = scanController->enterEXTEST();
            modeName = "EXTEST";
            currentJTAGMode = JTAGMode::EXTEST;
            break;

        case 2: // BYPASS
            success = scanController->enterBYPASS();
            modeName = "BYPASS";
            currentJTAGMode = JTAGMode::BYPASS;
            break;

        default:
            QMessageBox::warning(this, "Invalid Mode",
                "Unknown JTAG mode selected");
            return;
    }

    if (success) {
        updateStatusBar(QString("Switched to %1 mode").arg(modeName));
        // Update table cell editability based on new mode
        updatePinsTable();
    } else {
        QMessageBox::critical(this, "Mode Change Failed",
            QString("Could not switch to %1 mode.\n"
                   "Check connection and device state.").arg(modeName));
    }
}

void MainWindow::onSetAllToSafeState()
{
    if (!scanController) return;

    // Use the menu action implementation which already exists
    onSetAllDevicePinsToBSDLSafe();
}

void MainWindow::onSetAllTo1()
{
    if (!scanController) return;

    // Get all output pins and set them to HIGH
    auto pinList = scanController->getPinList();
    int count = 0;

    for (const auto& pinName : pinList) {
        std::string type = scanController->getPinType(pinName);
        if (type == "OUTPUT" || type == "INOUT") {
            if (scanController->setPin(pinName, JTAG::PinLevel::HIGH)) {
                count++;
            }
        }
    }

    scanController->applyChanges();
    updateStatusBar(QString("Set %1 output pins to HIGH").arg(count));
    updatePinsTable();
}

void MainWindow::onSetAllToZ()
{
    if (!scanController) return;

    // Get all output pins and set them to HIGH_Z
    auto pinList = scanController->getPinList();
    int count = 0;

    for (const auto& pinName : pinList) {
        std::string type = scanController->getPinType(pinName);
        if (type == "OUTPUT" || type == "INOUT") {
            if (scanController->setPin(pinName, JTAG::PinLevel::HIGH_Z)) {
                count++;
            }
        }
    }

    scanController->applyChanges();
    updateStatusBar(QString("Set %1 output pins to High-Z").arg(count));
    updatePinsTable();
}

void MainWindow::onSetAllTo0()
{
    if (!scanController) return;

    // Get all output pins and set them to LOW
    auto pinList = scanController->getPinList();
    int count = 0;

    for (const auto& pinName : pinList) {
        std::string type = scanController->getPinType(pinName);
        if (type == "OUTPUT" || type == "INOUT") {
            if (scanController->setPin(pinName, JTAG::PinLevel::LOW)) {
                count++;
            }
        }
    }

    scanController->applyChanges();
    updateStatusBar(QString("Set %1 output pins to LOW").arg(count));
    updatePinsTable();
}
