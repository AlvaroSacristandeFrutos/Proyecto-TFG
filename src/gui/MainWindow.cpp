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
#include <iostream>
#include <iomanip>

// IMPORTANTE: Descomenta estas líneas cuando integres el backend
#include "../controller/ScanController.h"
#include "../hal/JtagProtocol.h"  // Para PinLevel enum

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
    , ui(new Ui::MainWindow)
    , scanController(nullptr)  // AQUÍ INICIALIZARÁS: std::make_unique<JTAG::ScanController>()
    , waveformScene(nullptr)
    , chipVisualizer(nullptr)
    , zoomComboBox(nullptr)
    , inoutActionGroup(nullptr)
    , currentZoom(1.0)
    , isAdapterConnected(false)
    , isDeviceDetected(false)
    , isDeviceInitialized(false)
    , isCapturing(false)
    , waveformTimebase(1.0)
{
    ui->setupUi(this);
    
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
    // ==================== PUNTO DE INTEGRACIÓN 1 ====================
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
}

// ============================================================================
// FILE MENU SLOTS
// ============================================================================

void MainWindow::onNewProjectWizard()
{
    QMessageBox::information(this, "New Project", "New Project Wizard - To be implemented");
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
    // ==================== PUNTO DE INTEGRACIÓN 2 ====================
    if (!scanController) {
        QMessageBox::critical(this, "Error", "ScanController not initialized");
        return;
    }

    // 1. Obtener lista de adaptadores detectados
    auto adapters = scanController->getDetectedAdapters();

    // DEBUG: Verificar qué se detectó
    if (adapters.empty()) {
        QMessageBox::warning(this, "No Adapters",
            "No adapters detected. MockAdapter should always be available.\n\n"
            "This might be a configuration issue.");
        return;
    }

    // 2. Mostrar diálogo para que usuario elija
    QStringList adapterNames;
    for (const auto& adapter : adapters) {
        adapterNames << QString::fromStdString(adapter.name);
    }

    // DEBUG: Mostrar cuántos adaptadores se encontraron
    updateStatusBar(QString("Found %1 adapter(s)").arg(adapters.size()));

    bool ok;
    QString selected = QInputDialog::getItem(this, "Select JTAG Adapter",
                                             "Available adapters:", adapterNames, 0, false, &ok);

    if (ok && !selected.isEmpty()) {
        // 3. Encontrar el tipo de adaptador seleccionado
        JTAG::AdapterType selectedType;
        bool found = false;
        for (const auto& adapter : adapters) {
            if (QString::fromStdString(adapter.name) == selected) {
                selectedType = adapter.type;
                found = true;
                break;
            }
        }

        if (!found) {
            QMessageBox::critical(this, "Error", "Selected adapter not found");
            return;
        }

        // 4. Conectar
        if (scanController->connectAdapter(selectedType, 1000000)) {
            isAdapterConnected = true;
            updateStatusBar("Connected to " + selected);

            // Auto-detectar IDCODE y cargar BSDL
            uint32_t idcode = scanController->detectDevice();

            // DIAGNÓSTICO 1: Mostrar IDCODE detectado
            QMessageBox::information(this, "DEBUG 1 - IDCODE Detection",
                QString("IDCODE detectado: 0x%1\n\nDecimal: %2")
                    .arg(idcode, 8, 16, QChar('0'))
                    .arg(idcode));

            if (idcode != 0) {
                isDeviceDetected = true;

                // DIAGNÓSTICO 2: Verificar catálogo antes de autoLoadBSDL
                size_t catalogSize = scanController->getCatalogSize();
                QMessageBox::information(this, "DEBUG 2 - Catalog Status",
                    QString("Intentando autoLoadBSDL()...\n\n"
                            "Catalog size: %1 entries\n"
                            "Buscando IDCODE: 0x%2")
                        .arg(catalogSize)
                        .arg(idcode, 8, 16, QChar('0')));

                if (scanController->autoLoadBSDL()) {
                    // DIAGNÓSTICO 3: BSDL cargado exitosamente
                    QMessageBox::information(this, "DEBUG 3 - Success",
                        QString("BSDL loaded successfully!\n\n"
                                "Device: %1\n"
                                "IDCODE: 0x%2")
                            .arg(QString::fromStdString(scanController->getDeviceName()))
                            .arg(idcode, 8, 16, QChar('0')));

                    if (scanController->initializeDevice()) {
                        isDeviceInitialized = true;
                        updateStatusBar(QString("Device 0x%1 - BSDL loaded")
                            .arg(idcode, 8, 16, QChar('0')));
                        updatePinsTable();
                        renderChipVisualization();
                    }
                } else {
                    // DIAGNÓSTICO 4: Mostrar por qué falló
                    QMessageBox::warning(this, "DEBUG 4 - FAILED",
                        QString("autoLoadBSDL() FAILED\n\n"
                                "IDCODE buscado: 0x%1\n"
                                "Catalog size: %2 entries\n\n"
                                "Posibles causas:\n"
                                "- Archivo BSDL no existe\n"
                                "- IDCODE no coincide\n"
                                "- Error al parsear")
                            .arg(idcode, 8, 16, QChar('0'))
                            .arg(catalogSize));

                    updateStatusBar(QString("Device 0x%1 - No BSDL found")
                        .arg(idcode, 8, 16, QChar('0')));
                }
            } else {
                QMessageBox::warning(this, "DEBUG - No Device",
                    "IDCODE is 0 - No device detected");
            }

            enableControlsAfterConnection(true);
        } else {
            QMessageBox::critical(this, "Connection Error",
                                "Failed to connect to adapter: " + selected);
        }
    }
    // ================================================================
}

void MainWindow::onExamineChain()
{
    // ==================== PUNTO DE INTEGRACIÓN 3 ====================
    if (!scanController || !isAdapterConnected) {
        QMessageBox::warning(this, "Not Connected", "Please connect to a JTAG adapter first");
        return;
    }

    uint32_t idcode = scanController->detectDevice();
    if (idcode != 0) {
        isDeviceDetected = true;
        updateStatusBar(QString("Device detected - IDCODE: 0x%1").arg(idcode, 8, 16, QChar('0')));

        // Llenar el combo de dispositivos
        ui->comboBoxDevice->clear();
        ui->comboBoxDevice->addItem(QString("Device 0x%1").arg(idcode, 8, 16, QChar('0')));

        enableControlsAfterConnection(true);
    } else {
        QMessageBox::warning(this, "No Device", "No device detected on JTAG chain");
    }
    // ================================================================
}

void MainWindow::onRun()
{
    // ==================== PUNTO DE INTEGRACIÓN 4 ====================
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
    // ================================================================
}

void MainWindow::onJTAGReset()
{
    // ==================== PUNTO DE INTEGRACIÓN 5 ====================
    if (scanController && isAdapterConnected) {
        if (scanController->reset()) {
            updateStatusBar("JTAG TAP reset successful");
        } else {
            QMessageBox::critical(this, "Error", "JTAG reset failed");
        }
    }
    // ================================================================
}

void MainWindow::onDeviceInstruction()
{
    // Diálogo para seleccionar instrucción (SAMPLE, EXTEST, BYPASS, etc.)
    QMessageBox::information(this, "Device Instruction", "Device Instruction dialog - To be implemented");
}

void MainWindow::onDeviceBSDLFile()
{
    // ==================== PUNTO DE INTEGRACIÓN 6 ====================
    QString fileName = QFileDialog::getOpenFileName(this,
        tr("Open BSDL File"), "", tr("BSDL Files (*.bsd *.bsdl);;All Files (*)"));

    if (!fileName.isEmpty() && scanController) {
        if (scanController->loadBSDL(fileName.toStdString())) {
            updateStatusBar("BSDL loaded: " + fileName);

            // Inicializar dispositivo
            if (scanController->initializeDevice()) {
                isDeviceInitialized = true;
                enableControlsAfterConnection(true);

                // Actualizar tabla de pines
                updatePinsTable();

                // Actualizar visualización del chip
                renderChipVisualization();
            }
        } else {
            QMessageBox::critical(this, "Error", "Failed to load BSDL file");
        }
    }
    // ================================================================
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
    info += "  IR Length: " + QString::number(scanController->getDeviceName().empty() ? 0 : 8) + " bits\n";
    info += "  BSR Length: " + QString::number(scanController->getDeviceName().empty() ? 0 : 32) + " bits\n";
    info += "  Pin Count: " + QString::number(scanController->getPinList().size()) + "\n";

    QMessageBox::information(this, "Device Package Information", info);
}

void MainWindow::onDeviceProperties()
{
    // ==================== PUNTO DE INTEGRACIÓN 7 ====================
    if (!scanController || !isDeviceDetected) {
        QMessageBox::warning(this, "No Device", "No device detected");
        return;
    }

    QString info;
    info += "Device Name: " + QString::fromStdString(scanController->getDeviceName()) + "\n";
    info += "IDCODE: 0x" + QString::number(scanController->getIDCODE(), 16).toUpper() + "\n";
    info += "Adapter: " + QString::fromStdString(scanController->getAdapterInfo()) + "\n";

    QMessageBox::information(this, "Device Properties", info);
    // ================================================================
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

    QString pinName = nameItem->text();
    chipVisualizer->highlightPin(pinName);
}

void MainWindow::onEditPinNamesAndBuses()
{
    QMessageBox::information(this, "Edit Pin Names", "Edit Pin Names and Buses - To be implemented");
}

void MainWindow::onSetTo0()
{
    // ==================== PUNTO DE INTEGRACIÓN 8 ====================
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
    // ================================================================
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
    // ==================== PUNTO DE INTEGRACIÓN 9 ====================
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
    // ================================================================
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

    // CAMBIO: Limpiar tabla antes de llenar (permite recargar BSDL)
    ui->tableWidgetPins->setRowCount(0);

    // Llenar tabla con información completa
    for (const auto& pinName : pinNames) {
        int row = ui->tableWidgetPins->rowCount();
        ui->tableWidgetPins->insertRow(row);

        // Columna 0: Name
        ui->tableWidgetPins->setItem(row, 0,
            new QTableWidgetItem(QString::fromStdString(pinName)));

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

    // Actualizar valores I/O en tabla Y en visualizador
    for (int row = 0; row < ui->tableWidgetPins->rowCount(); row++) {
        QTableWidgetItem *nameItem = ui->tableWidgetPins->item(row, 0);
        if (nameItem) {
            std::string pinName = nameItem->text().toStdString();
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
                }

                // Actualizar visualizador del chip
                if (chipVisualizer) {
                    chipVisualizer->updatePinState(QString::fromStdString(pinName), visualState);
                }

                // DEBUG: Primer pin para verificar
                if (row == 0) {
                    qDebug() << "[updatePinsTable] Pin" << QString::fromStdString(pinName)
                             << "= " << valueStr << "-> visualState:" << (int)visualState;
                }
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

    // Mostrar en barra de estado que recibimos datos (DEBUG)
    static int updateCount = 0;
    updateCount++;
    statusBar()->showMessage(QString("Updates received: %1 (pins: %2)")
                            .arg(updateCount).arg(pins.size()), 100);

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
