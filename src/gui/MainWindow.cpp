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
#include <QDir>
#include <QStringList>
#include <QTimer>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QSplitter>
#include <QScrollBar>
#include <QDialog>
#include <QListWidget>
#include <QDialogButtonBox>
#include <iostream>
#include <iomanip>
#include <cmath>
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
    , timelineScene(nullptr)
    , timelineView(nullptr)
    , chipVisualizer(nullptr)
    , controlPanel(nullptr)
    , zoomComboBox(nullptr)
    , radioSample(nullptr)
    , radioExtest(nullptr)
    , radioIntest(nullptr)
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
    , isRedrawing(false)
{
    ui->setupUi(this);

    qRegisterMetaType<std::vector<JTAG::PinLevel>>("std::vector<JTAG::PinLevel>");
    qRegisterMetaType<JTAG::PinLevel>("JTAG::PinLevel");

    initializeUI();
    setupGraphicsViews();
    setupTables();
    setupToolbar();

    // Crear Control Panel ANTES de setupConnections() para poder conectar la señal
    controlPanel = new ControlPanelWidget(this);

    // Reemplazar widget en dockWatch
    QWidget* oldWidget = ui->dockWatch->widget();
    ui->dockWatch->setWidget(controlPanel);
    delete oldWidget;  // Eliminar tableWidgetWatch

    // Hide Watch panel by default
    ui->dockWatch->setVisible(false);
    ui->actionWatch->setChecked(false);

    setupBackend();
    setupConnections();  // Ahora controlPanel YA existe cuando conectamos

    updateWindowTitle();
    enableControlsAfterConnection(false);

    // Asegurar que el DockWidget Waveform es visible y tiene tamaño razonable
    ui->dockWaveform->setVisible(true);
    ui->dockWaveform->resize(1200, 300);
    ui->actionWaveform->setChecked(true);
}

MainWindow::~MainWindow()
{
    // Detener polling si está activo
    if (scanController && isCapturing) {
        scanController->stopPolling();
    }
    delete waveformScene;
    delete timelineScene;
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

    // DESHABILITADO: Inicialización del catálogo BSDL (causa lentitud al inicio)
    // El catálogo se cargará bajo demanda cuando se detecte un dispositivo
    // o cuando el usuario cargue manualmente un BSDL
    /*
    // Buscar test_files en varios paths posibles
    QStringList possiblePaths = {
        QCoreApplication::applicationDirPath() + "/../../test_files",  // build/Debug
        QCoreApplication::applicationDirPath() + "/../../../test_files", // out/build/debug
        "C:/Proyecto/BoundaryScanner/test_files" // Fallback absoluto
    };

    QString testFilesPath;
    for (const QString& path : possiblePaths) {
        QDir dir(path);
        if (dir.exists()) {
            testFilesPath = dir.absolutePath();
            qDebug() << "[MainWindow] Found test_files at:" << testFilesPath;
            break;
        }
    }

    if (testFilesPath.isEmpty()) {
        qDebug() << "[MainWindow] WARNING: test_files directory not found!";
        testFilesPath = QCoreApplication::applicationDirPath() + "/../../test_files"; // Usar default
    }

    scanController->initializeBSDLCatalog(testFilesPath.toStdString());
    */

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

    // Setup waveform graphics view with timeline
    waveformScene = new QGraphicsScene(this);
    waveformNamesScene = new QGraphicsScene(this);
    timelineScene = new QGraphicsScene(this);

    // Create timeline view (fixed height at top)
    timelineView = new QGraphicsView(this);
    timelineView->setScene(timelineScene);
    timelineView->setRenderHint(QPainter::Antialiasing);
    timelineView->setFixedHeight(50);
    timelineView->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    timelineView->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    timelineView->setStyleSheet("background-color: rgb(245, 245, 245); border-bottom: 1px solid rgb(200, 200, 200);");

    // Create fixed names view (left side, 150px wide)
    waveformNamesView = new QGraphicsView(this);
    waveformNamesView->setScene(waveformNamesScene);
    waveformNamesView->setRenderHint(QPainter::Antialiasing);
    waveformNamesView->setFixedWidth(150);
    waveformNamesView->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    waveformNamesView->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    waveformNamesView->setStyleSheet("background-color: rgb(245, 245, 245); border-right: 2px solid rgb(180, 180, 180);");

    // Configure existing waveform view
    ui->graphicsViewWaveform->setScene(waveformScene);
    ui->graphicsViewWaveform->setRenderHint(QPainter::Antialiasing);

    // Sincronizar scroll vertical entre nombres y waveform
    connect(ui->graphicsViewWaveform->verticalScrollBar(), &QScrollBar::valueChanged,
            [this](int value) {
                waveformNamesView->verticalScrollBar()->setValue(value);
            });

    // BUG FIX 2 & 3: Sincronizar scroll HORIZONTAL entre waveform y timeline
    // Y redibujar para actualizar eje temporal con nuevos timestamps visibles
    connect(ui->graphicsViewWaveform->horizontalScrollBar(), &QScrollBar::valueChanged,
            [this](int value) {
                if (!isRedrawing) {  // Solo si no estamos ya en redibujado
                    timelineView->horizontalScrollBar()->setValue(value);
                    redrawWaveform();  // Actualizar eje temporal con timestamps visibles
                }
            });

    // Get splitter from UI (para eliminarlo y reemplazarlo)
    QSplitter* splitter = ui->splitter;

    // Create horizontal container for names + waveform
    QWidget* waveformRow = new QWidget();
    QHBoxLayout* waveformRowLayout = new QHBoxLayout(waveformRow);
    waveformRowLayout->setContentsMargins(0, 0, 0, 0);
    waveformRowLayout->setSpacing(0);
    waveformRowLayout->addWidget(waveformNamesView);
    waveformRowLayout->addWidget(ui->graphicsViewWaveform);

    // CRITICAL: Set expanding policy for waveform row (takes all remaining space)
    waveformRow->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);

    // Create horizontal container for timeline with 150px left spacer (to align with names)
    QWidget* timelineRow = new QWidget();
    QHBoxLayout* timelineRowLayout = new QHBoxLayout(timelineRow);
    timelineRowLayout->setContentsMargins(0, 0, 0, 0);
    timelineRowLayout->setSpacing(0);

    // Add spacer widget with same width as waveformNamesView (150px)
    QWidget* timelineSpacer = new QWidget();
    timelineSpacer->setFixedWidth(150);
    timelineSpacer->setStyleSheet("background-color: rgb(245, 245, 245);");
    timelineRowLayout->addWidget(timelineSpacer);
    timelineRowLayout->addWidget(timelineView);

    // CRITICAL: Set fixed height for timeline row (no expansion)
    timelineRow->setFixedHeight(50);
    timelineRow->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);

    // Create container widget to hold timeline row and waveform row
    QWidget* waveformContainer = new QWidget();
    waveformContainer->setObjectName("waveformContainer");
    QVBoxLayout* waveformLayout = new QVBoxLayout(waveformContainer);
    waveformLayout->setContentsMargins(0, 0, 0, 0);
    waveformLayout->setSpacing(0);

    // CRITICAL: Add with stretch factors: timeline=0 (fixed), waveform=1 (expanding)
    waveformLayout->addWidget(timelineRow, 0);      // No stretch - fixed height
    waveformLayout->addWidget(waveformRow, 1);      // Stretch=1 - takes all remaining space

    // Reemplazar el splitter en el layout del DockWidget
    QLayout* dockLayout = splitter->parentWidget()->layout();
    int splitterIndex = dockLayout->indexOf(splitter);

    if (splitterIndex >= 0) {
        // Remover splitter del layout
        dockLayout->removeWidget(splitter);

        // Ocultar y eliminar table y splitter
        ui->tableWidgetWaveform->setParent(nullptr);
        ui->tableWidgetWaveform->deleteLater();
        splitter->setParent(nullptr);
        splitter->deleteLater();

        // Insertar container en su lugar
        dockLayout->addWidget(waveformContainer);

        std::cout << "[DEBUG] Splitter and table removed, waveformContainer added at full width" << std::endl;
    } else {
        std::cout << "[ERROR] Could not find splitter in dock layout!" << std::endl;
    }

    // Configurar política de tamaño
    waveformContainer->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    waveformContainer->show();
    timelineView->show();
    ui->graphicsViewWaveform->show();

    // BUG FIX 1: NO dibujar waveform inicial vacío (se inicializa al añadir primera señal)
    // redrawWaveform();  // ELIMINADO - evita grid colapsado al iniciar
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

    // Setup Waveform table - SOLO columna Name
    ui->tableWidgetWaveform->setColumnCount(1);
    ui->tableWidgetWaveform->setHorizontalHeaderLabels(QStringList() << "Name");
    ui->tableWidgetWaveform->setSelectionBehavior(QAbstractItemView::SelectRows);

    // Configurar altura de fila fija (40px) para alinearse con waveform
    ui->tableWidgetWaveform->verticalHeader()->setDefaultSectionSize(40);
    ui->tableWidgetWaveform->verticalHeader()->setSectionResizeMode(QHeaderView::Fixed);

    // Configurar ancho de columna para que use todo el espacio disponible
    ui->tableWidgetWaveform->horizontalHeader()->setStretchLastSection(true);
    ui->tableWidgetWaveform->setColumnWidth(0, 150);  // Name (ancho inicial)
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
    radioSampleSingleShot = new QRadioButton("SAMPLE 1x", this);
    radioExtest = new QRadioButton("EXTEST", this);
    radioIntest = new QRadioButton("INTEST", this);
    radioBypass = new QRadioButton("BYPASS", this);

    radioSample->setChecked(true); // Default to SAMPLE mode
    radioSampleSingleShot->setToolTip("Single shot sample - captures once and stops");

    // Create button group
    jtagModeButtonGroup = new QButtonGroup(this);
    jtagModeButtonGroup->addButton(radioSample, 0);
    jtagModeButtonGroup->addButton(radioSampleSingleShot, 1);
    jtagModeButtonGroup->addButton(radioExtest, 2);
    jtagModeButtonGroup->addButton(radioIntest, 3);
    jtagModeButtonGroup->addButton(radioBypass, 4);

    // Add to toolbar
    ui->toolBar->addWidget(radioSample);
    ui->toolBar->addWidget(radioSampleSingleShot);
    ui->toolBar->addWidget(radioExtest);
    ui->toolBar->addWidget(radioIntest);
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
    radioSampleSingleShot->setEnabled(false);
    radioExtest->setEnabled(false);
    radioIntest->setEnabled(false);
    radioBypass->setEnabled(false);
    btnSetAllSafe->setEnabled(false);
    btnSetAll1->setEnabled(false);
    btnSetAllZ->setEnabled(false);
    btnSetAll0->setEnabled(false);

    // Permanently disable BYPASS and INTEST modes (not implemented)
    radioBypass->setToolTip("BYPASS mode - Not available");
    radioIntest->setToolTip("INTEST mode - Not available");
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
    // Instruction action removed - use Device > Instruction instead if needed
    // connect(ui->actionInstruction, &QAction::triggered, this, &MainWindow::onInstruction);
    ui->actionInstruction->setVisible(false);  // Hide from toolbar

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

    // Control Panel connection
    if (controlPanel) {
        bool connected = connect(controlPanel, &ControlPanelWidget::pinValueChanged,
                                this, &MainWindow::onControlPanelPinChanged);
        qDebug() << "[MainWindow::setupConnections] Control Panel signal connected:" << connected;
    } else {
        qDebug() << "[MainWindow::setupConnections] ERROR: controlPanel is null!";
    }
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
    radioSampleSingleShot->setEnabled(enable && isDeviceInitialized);
    radioExtest->setEnabled(enable && isDeviceInitialized);
    // radioIntest and radioBypass permanently disabled (not implemented)
    radioIntest->setEnabled(false);
    radioBypass->setEnabled(false);
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
        JTAG::AdapterDescriptor descriptor = dialog.getSelectedDescriptor();
        uint32_t clockSpeed = dialog.getSelectedClockSpeed();

        // 3. Conectar y detectar dispositivo automáticamente
        if (scanController->connectAdapter(descriptor, clockSpeed)) {
            isAdapterConnected = true;

            // Use descriptor info directly (includes serial number)
            QString adapterName = QString::fromStdString(descriptor.name);
            QString serialInfo = QString::fromStdString(descriptor.serialNumber);

            updateStatusBar(QString("Connected to %1 (%2) @ %3 Hz")
                .arg(adapterName)
                .arg(serialInfo)
                .arg(clockSpeed));

            enableControlsAfterConnection(true);

            // Detectar dispositivo automáticamente
            QTimer::singleShot(500, this, [this]() {
                uint32_t idcode = scanController->detectDevice();

                if (idcode != 0 && idcode != 0xFFFFFFFF) {
                    isDeviceDetected = true;

                    // Decodificar IDCODE
                    uint8_t version = (idcode >> 28) & 0xF;
                    uint16_t partNumber = (idcode >> 12) & 0xFFFF;
                    uint16_t manufacturer = (idcode >> 1) & 0x7FF;

                    // Crear mensaje
                    QString message = QString(
                        "<b style='font-size:14pt;'>Device Detected</b><br><br>"
                        "<b>IDCODE:</b> 0x%1<br><br>"
                        "<b>Manufacturer ID:</b> 0x%2<br>"
                        "<b>Part Number:</b> 0x%3<br>"
                        "<b>Version:</b> 0x%4"
                    )
                    .arg(idcode, 8, 16, QChar('0'))
                    .arg(manufacturer, 3, 16, QChar('0'))
                    .arg(partNumber, 4, 16, QChar('0'))
                    .arg(version, 1, 16);

                    // Crear popup temporal (con botón OK para cerrar manualmente)
                    QMessageBox* msgBox = new QMessageBox(this);
                    msgBox->setWindowTitle("JTAG Device Detection");
                    msgBox->setText(message);
                    msgBox->setIcon(QMessageBox::Information);
                    msgBox->setStandardButtons(QMessageBox::Ok);
                    msgBox->setAttribute(Qt::WA_DeleteOnClose);

                    // Configurar timer para auto-cerrar
                    QTimer* autoCloseTimer = new QTimer(msgBox);
                    autoCloseTimer->setSingleShot(true);
                    connect(autoCloseTimer, &QTimer::timeout, msgBox, &QMessageBox::close);
                    autoCloseTimer->start(5000);

                    // Si el usuario cierra manualmente, cancelar el timer
                    connect(msgBox, &QMessageBox::finished, autoCloseTimer, &QTimer::stop);

                    msgBox->show();

                    // Actualizar combo
                    ui->comboBoxDevice->clear();
                    ui->comboBoxDevice->addItem(
                        QString("Device 0x%1").arg(idcode, 8, 16, QChar('0')));

                    updateStatusBar(QString("Device detected - IDCODE: 0x%1")
                        .arg(idcode, 8, 16, QChar('0')));

                    // TODO: Decidir si lanzar New Project Wizard automáticamente
                    // Actualmente comentado - el usuario debe cargarlo manualmente
                    // QTimer::singleShot(5500, this, &MainWindow::onNewProjectWizard);
                }
            });
        } else {
            // Mensaje de error detallado según el tipo de adaptador
            QString errorMsg = "Failed to connect to adapter.\n\n";

            switch (descriptor.type) {
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
        // W2: Limpiar buffers de waveform para nueva captura
        for (auto& [name, samples] : waveformBuffer) {
            samples.clear();
        }

        // Entrar en modo SAMPLE para capturar pines (el worker lo maneja)
        if (scanController->enterSAMPLE()) {
            isCapturing = true;
            captureTimer.restart();  // W1: Resetear a 0 (no continuar desde antes)
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
    if (!scanController) {
        QMessageBox::warning(this, "No Controller", "ScanController not initialized");
        return;
    }

    // Confirmar acción
    QMessageBox::StandardButton reply = QMessageBox::question(
        this,
        "JTAG Reset",
        "This will disconnect the adapter and unload the BSDL file.\n\n"
        "Do you want to continue?",
        QMessageBox::Yes | QMessageBox::No
    );

    if (reply == QMessageBox::Yes) {
        // Detener polling si está activo
        if (isCapturing) {
            scanController->stopPolling();
            isCapturing = false;
            ui->actionRun->setText("Run");
        }

        // Desconectar adapter y descargar BSDL
        scanController->disconnectAdapter();

        // Actualizar estado de la UI
        isAdapterConnected = false;
        isDeviceDetected = false;
        isDeviceInitialized = false;

        // Limpiar controles
        ui->comboBoxDevice->clear();
        ui->tableWidgetPins->setRowCount(0);

        if (controlPanel) {
            controlPanel->removeAllPins();
            ui->dockWatch->setVisible(false);
        }

        // Resetear visualización del chip (limpiar escena completamente)
        if (chipVisualizer) {
            chipVisualizer->scene()->clear();
            chipVisualizer->update();
        }

        // Deshabilitar controles
        enableControlsAfterConnection(false);

        // Resetear modo JTAG a SAMPLE
        if (radioSample) {
            radioSample->setChecked(true);
        }
        currentJTAGMode = JTAGMode::SAMPLE;

        updateStatusBar("JTAG Reset: Adapter disconnected, BSDL unloaded");

        QMessageBox::information(this, "JTAG Reset Complete",
            "Adapter disconnected and BSDL unloaded successfully.\n\n"
            "You can now connect a new adapter or device.");
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
    if (!controlPanel) return;

    // Obtener pin seleccionado en tabla Pins
    int currentRow = ui->tableWidgetPins->currentRow();
    if (currentRow < 0) {
        QMessageBox::information(this, "No Selection",
            "Please select a pin in the Pins table first");
        return;
    }

    // Extraer información del pin
    QTableWidgetItem* nameItem = ui->tableWidgetPins->item(currentRow, 0);
    QTableWidgetItem* pinNumItem = ui->tableWidgetPins->item(currentRow, 1);

    if (!nameItem || !pinNumItem) return;

    std::string pinName = nameItem->text().toStdString();
    std::string pinNumber = pinNumItem->text().toStdString();

    // Añadir al Control Panel
    controlPanel->addPin(pinName, pinNumber);

    // Mostrar dock si está oculto
    ui->dockWatch->setVisible(true);

    updateStatusBar(QString("Added %1 to Control Panel")
        .arg(QString::fromStdString(pinName)));
}

void MainWindow::onWatchRemove()
{
    if (!controlPanel) return;

    std::string selectedPin = controlPanel->getSelectedPin();
    if (selectedPin.empty()) {
        QMessageBox::information(this, "No Selection",
            "Please select a pin to remove");
        return;
    }

    controlPanel->removePin(selectedPin);
    updateStatusBar(QString("Removed %1 from Control Panel")
        .arg(QString::fromStdString(selectedPin)));
}

void MainWindow::onWatchRemoveAll()
{
    if (!controlPanel) return;

    controlPanel->removeAllPins();
    updateStatusBar("Control Panel cleared");
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

    // BUG FIX 1: Detectar si es la primera vez que se añaden señales
    bool wasEmpty = waveformSignals.empty();

    for (int row : rows) {
        QTableWidgetItem *sourceItem = ui->tableWidgetPins->item(row, 0);
        if (sourceItem) {
            std::string pinName = sourceItem->text().toStdString();

            // Verificar que no exista ya
            if (std::find(waveformSignals.begin(), waveformSignals.end(), pinName)
                == waveformSignals.end()) {
                waveformSignals.push_back(pinName);
                waveformBuffer[pinName].clear();
            }
        }
    }

    updateStatusBar(QString("Added %1 signal(s) to Waveform").arg(rows.size()));

    // BUG FIX 1: Redibujar waveform para mostrar nombres y grid
    // Si era la primera señal, esto inicializa el grid/timeline correctamente
    if (wasEmpty && !waveformSignals.empty()) {
        // Primera inicialización del waveform - el grid se calculará correctamente
        redrawWaveform();
    } else {
        // Actualización normal
        redrawWaveform();
    }
}

void MainWindow::onWaveformRemove()
{
    if (waveformSignals.empty()) {
        updateStatusBar("No signals in waveform");
        return;
    }

    // Crear diálogo con lista de señales
    QDialog dialog(this);
    dialog.setWindowTitle("Remove Signals");
    QVBoxLayout *layout = new QVBoxLayout(&dialog);

    QLabel *label = new QLabel("Select signals to remove:", &dialog);
    layout->addWidget(label);

    QListWidget *listWidget = new QListWidget(&dialog);
    listWidget->setSelectionMode(QAbstractItemView::MultiSelection);
    for (const auto& pinName : waveformSignals) {
        listWidget->addItem(QString::fromStdString(pinName));
    }
    layout->addWidget(listWidget);

    QDialogButtonBox *buttonBox = new QDialogButtonBox(
        QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
    layout->addWidget(buttonBox);

    connect(buttonBox, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(buttonBox, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);

    if (dialog.exec() == QDialog::Accepted) {
        QList<QListWidgetItem*> selectedItems = listWidget->selectedItems();
        if (selectedItems.isEmpty()) {
            updateStatusBar("No signals selected");
            return;
        }

        // Obtener nombres de señales seleccionadas
        std::vector<std::string> removedPins;
        for (auto item : selectedItems) {
            removedPins.push_back(item->text().toStdString());
        }

        // Eliminar de waveformSignals
        for (const auto& pin : removedPins) {
            waveformSignals.erase(
                std::remove(waveformSignals.begin(), waveformSignals.end(), pin),
                waveformSignals.end());
            waveformBuffer.erase(pin);
        }

        updateStatusBar(QString("Removed %1 signal(s)").arg(removedPins.size()));
        redrawWaveform();
    }
}

void MainWindow::onWaveformRemoveAll()
{
    waveformSignals.clear();
    waveformBuffer.clear();
    updateStatusBar("Waveform signals cleared");

    // Redibujar waveform (vacío)
    redrawWaveform();
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
    if (waveformTimebase < 0.001) waveformTimebase = 0.001;  // Límite mínimo

    // Actualizar indicador de zoom en toolbar
    QString zoomText;
    if (waveformTimebase >= 1.0) {
        zoomText = QString("%1 s").arg(waveformTimebase, 0, 'f', 1);
    } else if (waveformTimebase >= 0.001) {
        zoomText = QString("%1 ms").arg(waveformTimebase * 1000.0, 0, 'f', 1);
    } else {
        zoomText = QString("%1 µs").arg(waveformTimebase * 1000000.0, 0, 'f', 1);
    }
    ui->actionWaveZoomValue->setText(zoomText);

    updateStatusBar(QString("Waveform zoom: %1/div").arg(zoomText));
    redrawWaveform();  // ✅ Redibujar con nuevo zoom
}

void MainWindow::onWaveformZoomOut()
{
    waveformTimebase *= 2.0;
    if (waveformTimebase > 100.0) waveformTimebase = 100.0;  // Límite máximo

    // Actualizar indicador de zoom en toolbar
    QString zoomText;
    if (waveformTimebase >= 1.0) {
        zoomText = QString("%1 s").arg(waveformTimebase, 0, 'f', 1);
    } else if (waveformTimebase >= 0.001) {
        zoomText = QString("%1 ms").arg(waveformTimebase * 1000.0, 0, 'f', 1);
    } else {
        zoomText = QString("%1 µs").arg(waveformTimebase * 1000000.0, 0, 'f', 1);
    }
    ui->actionWaveZoomValue->setText(zoomText);

    updateStatusBar(QString("Waveform zoom: %1/div").arg(zoomText));
    redrawWaveform();  // ✅ Redibujar con nuevo zoom
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

// Removed - use onDeviceInstruction() directly from Device menu
// void MainWindow::onInstruction()
// {
//     onDeviceInstruction();
// }

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
    // Calcular duración total de datos capturados
    double maxTime = 0;
    for (const auto& [name, samples] : waveformBuffer) {
        if (!samples.empty()) {
            maxTime = std::max(maxTime, samples.back().timestamp);
        }
    }

    if (maxTime <= 0) {
        updateStatusBar("No waveform data to fit");
        return;
    }

    // Obtener ancho visible del viewport (en pixels)
    // Ahora todo el viewport está disponible para el diagrama (nombres en vista separada)
    int availableWidth = ui->graphicsViewWaveform->viewport()->width();

    if (availableWidth <= 0) {
        updateStatusBar("Viewport too small");
        return;
    }

    // Calcular timebase necesario para que maxTime quepa en availableWidth
    // PIXELS_PER_SECOND = 100.0 / timebase
    // maxTime * PIXELS_PER_SECOND = availableWidth
    // maxTime * (100.0 / timebase) = availableWidth
    // timebase = (maxTime * 100.0) / availableWidth

    waveformTimebase = (maxTime * 100.0) / availableWidth;

    // Añadir margen del 10% para no que no quede pegado al borde
    waveformTimebase *= 1.1;

    // Actualizar indicador de zoom en toolbar
    QString zoomText;
    if (waveformTimebase >= 1.0) {
        zoomText = QString("%1 s").arg(waveformTimebase, 0, 'f', 1);
    } else if (waveformTimebase >= 0.001) {
        zoomText = QString("%1 ms").arg(waveformTimebase * 1000.0, 0, 'f', 1);
    } else {
        zoomText = QString("%1 µs").arg(waveformTimebase * 1000000.0, 0, 'f', 1);
    }
    ui->actionWaveZoomValue->setText(zoomText);

    updateStatusBar(QString("Fit: %1 s total in view").arg(maxTime, 0, 'f', 2));
    redrawWaveform();
}

void MainWindow::onWavePrev()
{
    onWaveformPreviousEvent();  // Delegar al método del menú
}

void MainWindow::onWaveNext()
{
    onWaveformNextEvent();  // Delegar al método del menú
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
    if (!scanController) return;

    const bool wasBlocked = ui->tableWidgetPins->signalsBlocked();
    ui->tableWidgetPins->blockSignals(true);

    // Obtener lista de pines del modelo
    std::vector<std::string> pinNames = scanController->getPinList();

    // Si es la primera carga (tabla vacía), la llenamos
    bool isFirstLoad = (ui->tableWidgetPins->rowCount() == 0);

    if (isFirstLoad) {
        ui->tableWidgetPins->setRowCount(0);
        for (const auto& pName : pinNames) {
            int row = ui->tableWidgetPins->rowCount();
            ui->tableWidgetPins->insertRow(row);

            QString qPinName = QString::fromStdString(pName);

            // Col 0: Name
            QTableWidgetItem* nameItem = new QTableWidgetItem(qPinName);
            nameItem->setData(Qt::UserRole, qPinName);
            ui->tableWidgetPins->setItem(row, 0, nameItem);

            // Col 1: Pin #
            QString pinNumStr = QString::fromStdString(scanController->getPinNumber(pName));
            ui->tableWidgetPins->setItem(row, 1, new QTableWidgetItem(pinNumStr));

            // Col 2: Port
            QString port = QString::fromStdString(scanController->getPinPort(pName));
            ui->tableWidgetPins->setItem(row, 2, new QTableWidgetItem(port));

            // Col 3: I/O Value (Inicial)
            ui->tableWidgetPins->setItem(row, 3, new QTableWidgetItem("?"));

            // Col 4: Type
            QString type = QString::fromStdString(scanController->getPinType(pName));
            ui->tableWidgetPins->setItem(row, 4, new QTableWidgetItem(type));
        }
    }

    // --- BUCLE DE ACTUALIZACIÓN ---
    for (int row = 0; row < ui->tableWidgetPins->rowCount(); row++) {

        // 1. Recuperar nombre (Columna 0) -> NECESARIO PARA DEFINIR pinName
        QTableWidgetItem* nameItem = ui->tableWidgetPins->item(row, 0);
        if (!nameItem) continue;

        QString displayName = nameItem->text();
        QString realName = resolveRealPinName(displayName);
        std::string pinName = realName.toStdString(); // <--- Aquí definimos pinName

        // 2. Leer estado del pin
        auto level = scanController->getPin(pinName);

        if (level.has_value()) {
            QString valueStr;
            VisualPinState visualState; // <--- Aquí definimos visualState

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

            // 3. Actualizar la celda de VALOR (Columna 3)
            QTableWidgetItem* valueItem = ui->tableWidgetPins->item(row, 3);
            if (valueItem) {
                valueItem->setText(valueStr);

                // --- Lógica de Edición (EXTEST) ---
                QString type = QString::fromStdString(scanController->getPinType(pinName));

                // Permitir editar si es EXTEST y es una salida (incluyendo output2 del hack)
                bool isEditable = (currentJTAGMode == JTAGMode::EXTEST) &&
                    (type == "OUTPUT" || type == "INOUT" || type == "output2");

                if (isEditable) {
                    valueItem->setFlags(valueItem->flags() | Qt::ItemIsEditable);
                    valueItem->setBackground(QColor(255, 255, 200)); // Amarillo
                }
                else {
                    valueItem->setFlags(valueItem->flags() & ~Qt::ItemIsEditable);
                    valueItem->setBackground(Qt::white);
                }
            }

            // 4. Actualizar visualizador del chip
            if (chipVisualizer) {
                chipVisualizer->updatePinState(realName, visualState);
            }

            // Log del primer pin para debug (Opcional)
            /*
            if (row == 0) {
                qDebug() << "[updatePinsTable] Pin" << realName
                         << "= " << valueStr;
            }
            */
        }
        else {
            // Si level no tiene valor (std::nullopt)
             // qDebug() << "Pin no value:" << realName;
        }
    }
    ui->tableWidgetPins->blockSignals(wasBlocked);
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

void MainWindow::updateControlPanel(const std::vector<JTAG::PinLevel>& pinLevels)
{
    // El Control Panel es SOLO para edición del usuario en modos EXTEST/INTEST
    // NO debe actualizarse automáticamente desde el backend, ya que eso
    // sobreescribiría las selecciones del usuario en los radio buttons

    // El flujo correcto es:
    // Usuario cambia radio button → emit pinValueChanged → setPinAsync →
    // backend actualiza → GUI mantiene el valor seleccionado por el usuario

    // Por tanto, este método NO hace nada intencionalmente
    Q_UNUSED(pinLevels);
}

void MainWindow::captureWaveformSample()
{
    // ==================== PUNTO DE INTEGRACIÓN 13 ====================
    if (!scanController || waveformSignals.empty()) return;

    double currentTime = captureTimer.elapsed() / 1000.0; // Convert ms to seconds

    // Iterar sobre vector de señales en lugar de tabla
    for (const std::string& pinName : waveformSignals) {
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

    // W4: Redraw waveform (throttle to avoid performance issues - every 2nd sample)
    static int redrawCounter = 0;
    if (++redrawCounter >= 2) {
        redrawCounter = 0;
        redrawWaveform();
    }
    // ================================================================
}

void MainWindow::redrawWaveform()
{
    // BUG FIX 3: Prevenir redibujado recursivo
    if (isRedrawing) return;
    isRedrawing = true;

    // BUG FIX 1: Si no hay señales añadidas, mantener waveform vacío (limpio)
    if (waveformSignals.empty()) {
        // Limpiar escenas y dejar todo en blanco
        waveformScene->clear();
        waveformNamesScene->clear();
        timelineScene->clear();
        isRedrawing = false;
        return;  // NO dibujar grid ni timeline cuando no hay señales
    }

    waveformScene->clear();
    waveformNamesScene->clear();
    timelineScene->clear();

    // FIXED SIGNAL_HEIGHT - Las señales NO se estiran al agrandar ventana
    const int SIGNAL_HEIGHT = 40;      // Vertical space per signal (FIXED)
    const int HIGH_Y_OFFSET = 10;      // Y offset for HIGH level
    const int LOW_Y_OFFSET = 30;       // Y offset for LOW level
    const double PIXELS_PER_SECOND = 100.0 / waveformTimebase; // Zoom factor

    // W7: Helper para calcular Y según nivel (maneja HIGH_Z)
    auto getLevelY = [&](JTAG::PinLevel level, int yBase) -> int {
        if (level == JTAG::PinLevel::HIGH) return yBase + HIGH_Y_OFFSET;
        if (level == JTAG::PinLevel::LOW) return yBase + LOW_Y_OFFSET;
        return yBase + 20;  // HIGH_Z en posición intermedia
    };

    // Calcular timestamp máximo de todos los buffers
    double maxTime = 0;
    for (const auto& [name, samples] : waveformBuffer) {
        if (!samples.empty()) {
            maxTime = std::max(maxTime, samples.back().timestamp);
        }
    }

    // BUG FIX 1: Si no hay datos, establecer escenario inicial consistente
    bool isEmpty = (maxTime < 0.1);
    if (isEmpty) {
        // Calcular tiempo mínimo basado en el ancho del viewport para distribución óptima
        int viewportWidthPixels = ui->graphicsViewWaveform->viewport()->width();
        if (viewportWidthPixels <= 0) viewportWidthPixels = 800;  // Fallback razonable

        // Queremos ~10 marcas visibles, cada una separada por ~100px
        // Tiempo necesario = (viewportWidth / 100) * waveformTimebase
        maxTime = std::max(10.0, (viewportWidthPixels / 100.0) * waveformTimebase);
    }

    // Grid debe cubrir hasta última muestra (SIN NAME_MARGIN, ya que nombres están en vista separada)
    int maxX = std::max(2000.0, (maxTime + 5.0) * PIXELS_PER_SECOND);
    int maxY = std::max(40, static_cast<int>(waveformSignals.size() * SIGNAL_HEIGHT));

    // ESTRATEGIA CORRECTA: Calcular viewport visible usando scrollbar position
    QScrollBar* hScrollBar = ui->graphicsViewWaveform->horizontalScrollBar();
    int scrollPos = (hScrollBar && !isEmpty) ? hScrollBar->value() : 0;  // Sin scroll si vacío
    int viewportWidthPixels = ui->graphicsViewWaveform->viewport()->width();
    if (viewportWidthPixels <= 0) viewportWidthPixels = 800;  // Seguridad

    // Calcular rango visible en PIXELS (posición de la escena)
    int visibleStartX = scrollPos;
    int visibleEndX = scrollPos + viewportWidthPixels;

    // Convertir a tiempo
    double visibleStartTime = visibleStartX / PIXELS_PER_SECOND;
    double visibleEndTime = visibleEndX / PIXELS_PER_SECOND;
    double visibleTimeRange = visibleEndTime - visibleStartTime;

    // VALIDACIÓN: Asegurar rango mínimo para evitar divisiones por 0
    if (visibleTimeRange < 0.0001 || !std::isfinite(visibleTimeRange)) {
        visibleTimeRange = 10.0;  // Fallback a 10 segundos
        visibleStartTime = 0.0;
        visibleEndTime = 10.0;
    }

    // Grid automático - ~10 marcas MAYORES con números
    const int TARGET_MAJOR_DIVISIONS = 10;
    double rawInterval = visibleTimeRange / TARGET_MAJOR_DIVISIONS;

    // VALIDACIÓN: rawInterval debe ser > 0
    if (rawInterval <= 0 || !std::isfinite(rawInterval)) {
        rawInterval = 1.0;  // Fallback a 1 segundo
    }

    // BUG FIX 2: Redondear a valores "bonitos" con soporte para escalas muy pequeñas
    double magnitude, normalized, multiplier;

    // Proteger contra rawInterval inválido
    if (rawInterval <= 0 || !std::isfinite(rawInterval)) {
        magnitude = 1.0;
        normalized = 1.0;
        multiplier = 1.0;
    } else {
        magnitude = std::pow(10.0, std::floor(std::log10(rawInterval)));
        normalized = rawInterval / magnitude;

        if (normalized <= 1.5) {
            multiplier = 1.0;
        } else if (normalized <= 3.0) {
            multiplier = 2.0;
        } else if (normalized <= 7.0) {
            multiplier = 5.0;
        } else {
            multiplier = 10.0;
        }
    }

    double gridMajorInterval = multiplier * magnitude;
    double gridMinorInterval = gridMajorInterval / 5.0;

    // VALIDACIÓN FINAL: Asegurar intervalos válidos (permitir hasta nanosegundos)
    if (gridMajorInterval <= 0 || !std::isfinite(gridMajorInterval) || gridMajorInterval > 1000.0) {
        // Si el intervalo es inválido o demasiado grande, usar 1 segundo
        gridMajorInterval = 1.0;
        gridMinorInterval = 0.2;
    }
    // Para zoom muy profundo, asegurar mínimo razonable (1 nanosegundo)
    if (gridMajorInterval < 1e-9) {
        gridMajorInterval = 1e-9;
        gridMinorInterval = 2e-10;
    }

    // BUG FIX 2: Determinar unidad y decimales (soporte para nanosegundos)
    int decimals;
    QString timeUnit;
    if (gridMajorInterval >= 1.0) {
        timeUnit = "s";
        decimals = (gridMajorInterval >= 10.0) ? 0 : 1;
    } else if (gridMajorInterval >= 0.001) {
        timeUnit = "ms";
        decimals = (gridMajorInterval >= 0.01) ? 0 : 1;
    } else if (gridMajorInterval >= 0.000001) {
        timeUnit = "µs";
        decimals = (gridMajorInterval >= 0.00001) ? 0 : 1;
    } else {
        timeUnit = "ns";
        decimals = (gridMajorInterval >= 0.00000001) ? 0 : 1;
    }

    // Calcular inicio del grid alineado al rango visible (con margen pequeño)
    double gridStart = std::floor((visibleStartTime - gridMajorInterval) / gridMajorInterval) * gridMajorInterval;
    if (gridStart < 0) gridStart = 0;

    double gridEnd = visibleEndTime + gridMajorInterval;

    // Dibujar SOLO en el rango visible + margen (optimización crítica)
    QPen gridMajorPen(QColor(180, 180, 180), 1);
    QPen gridMinorPen(QColor(230, 230, 230), 1);

    // Grid MENOR (subdivisiones sin números) - SOLO en rango visible
    for (double t = gridStart; t <= gridEnd; t += gridMinorInterval) {
        if (t < 0) continue;
        int x = static_cast<int>(t * PIXELS_PER_SECOND);
        waveformScene->addLine(x, 0, x, maxY, gridMinorPen);
        timelineScene->addLine(x, 0, x, 50, gridMinorPen);
    }

    // Grid MAYOR (marcas principales) - SOLO en rango visible
    for (double t = gridStart; t <= gridEnd; t += gridMajorInterval) {
        if (t < 0) continue;
        int x = static_cast<int>(t * PIXELS_PER_SECOND);
        waveformScene->addLine(x, 0, x, maxY, gridMajorPen);
        timelineScene->addLine(x, 0, x, 50, gridMajorPen);
    }

    // W8: Dibujar etiquetas de tiempo SOLO en rango visible
    QPen timelinePen(QColor(100, 100, 100));

    // Dibujar etiquetas SOLO en marcas mayores visibles (optimización)
    for (double t = gridStart; t <= gridEnd; t += gridMajorInterval) {
        if (t < 0) continue;

        int x = static_cast<int>(t * PIXELS_PER_SECOND);

        // Línea vertical de tick más oscura
        timelineScene->addLine(x, 30, x, 48, timelinePen);

        // BUG FIX 2: Etiqueta de tiempo con unidad dinámica (soporte para nanosegundos)
        QString timeLabel;
        if (timeUnit == "s") {
            timeLabel = QString("%1 s").arg(t, 0, 'f', decimals);
        } else if (timeUnit == "ms") {
            timeLabel = QString("%1 ms").arg(t * 1000.0, 0, 'f', decimals);
        } else if (timeUnit == "µs") {
            timeLabel = QString("%1 µs").arg(t * 1000000.0, 0, 'f', decimals);
        } else {  // ns
            timeLabel = QString("%1 ns").arg(t * 1000000000.0, 0, 'f', decimals);
        }

        QGraphicsTextItem *timeText = timelineScene->addText(timeLabel);
        timeText->setPos(x - 25, 5);
        timeText->setDefaultTextColor(QColor(40, 40, 40));
        timeText->setFont(QFont("Arial", 9, QFont::Bold));
    }

    // Línea horizontal base de la timeline
    timelineScene->addLine(0, 40, maxX, 40, QPen(QColor(150, 150, 150), 2));

    // Draw each signal
    for (int row = 0; row < waveformSignals.size(); row++) {
        std::string pinName = waveformSignals[row];
        auto& samples = waveformBuffer[pinName];

        int yBase = row * SIGNAL_HEIGHT;
        int yHigh = yBase + HIGH_Y_OFFSET;
        int yLow = yBase + LOW_Y_OFFSET;

        // Dibujar nombre en escena separada (waveformNamesScene)
        QGraphicsTextItem *label = waveformNamesScene->addText(QString::fromStdString(pinName));
        label->setPos(10, yBase + 10);
        label->setDefaultTextColor(Qt::black);
        label->setFont(QFont("Arial", 10, QFont::Bold));

        // Dibujar separador horizontal en escena de nombres
        waveformNamesScene->addLine(0, yBase + SIGNAL_HEIGHT, 150, yBase + SIGNAL_HEIGHT,
                                   QPen(QColor(180, 180, 180)));

        if (samples.empty()) continue;

        // Dibujar líneas de referencia para HIGH y LOW (muy tenues)
        QPen referencePen(QColor(230, 230, 230), 1, Qt::DashLine);
        waveformScene->addLine(0, yHigh, maxX, yHigh, referencePen);  // HIGH level
        waveformScene->addLine(0, yLow, maxX, yLow, referencePen);    // LOW level

        // Draw waveform
        QPen signalPen(Qt::blue, 2);

        // W6: Si solo hay 1 muestra, dibujar punto/marcador
        if (samples.size() == 1) {
            double x = samples[0].timestamp * PIXELS_PER_SECOND;
            int y = getLevelY(samples[0].level, yBase);

            // Dibujar pequeño círculo como marcador
            waveformScene->addEllipse(x - 3, y - 3, 6, 6, signalPen, QBrush(Qt::blue));
            continue;  // Saltar al siguiente pin
        }

        // BUG FIX 3: CULLING - Solo dibujar muestras en el rango visible
        // Buscar índice de inicio (primera muestra visible)
        size_t startIdx = 0;
        for (size_t i = 0; i < samples.size(); i++) {
            if (samples[i].timestamp >= visibleStartTime - 1.0) {  // -1s margen
                startIdx = (i > 0) ? i - 1 : 0;  // Incluir muestra anterior para continuidad
                break;
            }
        }

        // Buscar índice de fin (última muestra visible)
        size_t endIdx = samples.size();
        for (size_t i = startIdx; i < samples.size(); i++) {
            if (samples[i].timestamp > visibleEndTime + 1.0) {  // +1s margen
                endIdx = std::min(i + 1, samples.size());  // Incluir muestra siguiente
                break;
            }
        }

        // Calcular decimación si hay demasiadas muestras visibles
        size_t visibleCount = endIdx - startIdx;
        size_t step = 1;
        const size_t MAX_VISIBLE_SAMPLES = 5000;
        if (visibleCount > MAX_VISIBLE_SAMPLES) {
            step = visibleCount / MAX_VISIBLE_SAMPLES;
        }

        // Dibujar solo muestras en el rango visible (con decimación si es necesario)
        for (size_t i = startIdx + 1; i < endIdx; i += step) {
            double x1 = samples[i-1].timestamp * PIXELS_PER_SECOND;
            double x2 = samples[i].timestamp * PIXELS_PER_SECOND;

            int y1 = getLevelY(samples[i-1].level, yBase);
            int y2 = getLevelY(samples[i].level, yBase);

            // W7: Dibujar con estilo diferente para HIGH_Z
            if (samples[i-1].level == JTAG::PinLevel::HIGH_Z) {
                QPen zPen(Qt::gray, 2, Qt::DashLine);
                waveformScene->addLine(x1, y1, x2, y1, zPen);
            } else {
                // Horizontal line (hold previous level)
                waveformScene->addLine(x1, y1, x2, y1, signalPen);
            }

            // Vertical line (transition)
            if (y1 != y2) {
                waveformScene->addLine(x2, y1, x2, y2, signalPen);
            }
        }

        // Draw separator line
        waveformScene->addLine(0, yBase + SIGNAL_HEIGHT, maxX, yBase + SIGNAL_HEIGHT,
                              QPen(QColor(180, 180, 180)));
    }

    // Configurar tamaños de las escenas
    waveformScene->setSceneRect(0, 0, maxX, maxY);
    waveformNamesScene->setSceneRect(0, 0, 150, maxY);
    timelineScene->setSceneRect(0, 0, maxX, 50);

    // BUG FIX 4: NO hacer auto-scroll automático - interfiere con zoom/scroll manual
    // El usuario puede navegar manualmente o usar botones de navegación

    isRedrawing = false;
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

    // 2. Actualizar Control Panel (reemplaza updateWatchTable)
    updateControlPanel(pins);

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

    JTAG::ScanMode targetMode;
    QString modeName;
    bool showControlPanel = false;
    bool enableControlPanel = false;

    switch (modeId) {
    case 0:  // SAMPLE
        targetMode = JTAG::ScanMode::SAMPLE;
        modeName = "SAMPLE";
        showControlPanel = false;  // Ocultar
        break;
    case 1:  // SAMPLE SINGLE SHOT
        targetMode = JTAG::ScanMode::SAMPLE_SINGLE_SHOT;
        modeName = "SAMPLE (Single Shot)";
        showControlPanel = false;  // Ocultar
        break;
    case 2:  // EXTEST
        targetMode = JTAG::ScanMode::EXTEST;
        modeName = "EXTEST";
        showControlPanel = true;   // Mostrar
        enableControlPanel = true; // Habilitar edición
        break;
    case 3:  // INTEST
        targetMode = JTAG::ScanMode::INTEST;
        modeName = "INTEST";
        showControlPanel = true;   // Mostrar
        enableControlPanel = true; // Habilitar edición
        break;
    case 4:  // BYPASS
        targetMode = JTAG::ScanMode::BYPASS;
        modeName = "BYPASS";
        showControlPanel = false;  // Ocultar
        break;
    default: return;
    }

    scanController->setScanMode(targetMode);

    currentJTAGMode = (modeId == 0) ? JTAGMode::SAMPLE :
                      (modeId == 1) ? JTAGMode::SAMPLE_SINGLE_SHOT :
                      (modeId == 2) ? JTAGMode::EXTEST :
                      (modeId == 3) ? JTAGMode::INTEST :
                      JTAGMode::BYPASS;

    // Controlar visibilidad del Control Panel
    if (controlPanel) {
        ui->dockWatch->setVisible(showControlPanel);
        controlPanel->setEnabled(enableControlPanel);

        // Auto-poblar Control Panel con pines editables al entrar en EXTEST/INTEST
        if (showControlPanel && enableControlPanel && scanController) {
            controlPanel->removeAllPins();  // Limpiar primero

            auto pinList = scanController->getPinList();
            for (const auto& pinName : pinList) {
                std::string type = scanController->getPinType(pinName);
                // Solo añadir pines editables (OUTPUT y INOUT)
                if (type == "OUTPUT" || type == "INOUT" || type == "output2" || type == "inout2") {
                    std::string pinNumber = scanController->getPinNumber(pinName);
                    controlPanel->addPin(pinName, pinNumber);
                }
            }

            updateStatusBar(QString("Mode changed to %1 - Control Panel populated with editable pins").arg(modeName));
        } else {
            updateStatusBar(QString("Mode changed to %1").arg(modeName));
        }
    } else {
        updateStatusBar(QString("Mode changed to %1").arg(modeName));
    }

    updatePinsTable(); // Refrescar para habilitar/deshabilitar edición
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

void MainWindow::onControlPanelPinChanged(QString pinName, JTAG::PinLevel level)
{
    QString levelStr = (level == JTAG::PinLevel::LOW) ? "0" :
                       (level == JTAG::PinLevel::HIGH) ? "1" : "Z";

    qDebug() << "[MainWindow] Control panel pin changed - Pin:" << pinName
             << "Level:" << levelStr;

    if (!scanController) {
        qDebug() << "[MainWindow] ERROR: scanController is null";
        return;
    }

    // Convertir QString a std::string para el backend
    std::string pinNameStd = pinName.toStdString();

    // Llamar al backend de forma asíncrona
    qDebug() << "[MainWindow] Calling setPinAsync for pin:" << pinName;
    scanController->setPinAsync(pinNameStd, level);

    updateStatusBar(QString("Pin %1 set to %2")
        .arg(pinName)
        .arg(levelStr));
}
