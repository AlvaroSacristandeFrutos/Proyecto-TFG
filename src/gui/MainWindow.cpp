/**
 * @file mainwindow.cpp
 * @brief Implementación de la ventana principal de TopJTAG Probe
 *
 * Este archivo contiene la implementación completa de la ventana principal
 * de la aplicación de Boundary Scan JTAG. Gestiona:
 *
 * - Conexión y desconexión de adaptadores JTAG
 * - Carga de archivos BSDL para configuración de dispositivos
 * - Visualización y control de pines del Boundary Scan Register (BSR)
 * - Captura en tiempo real del estado de pines (polling)
 * - Visualización gráfica del chip con ChipVisualizer
 * - Panel de control para observar pines (ControlPanelWidget)
 * - Formas de onda digitales (waveform viewer)
 * - Modos JTAG: SAMPLE, EXTEST, INTEST, BYPASS
 * - Wizards: New Project, Chain Examine, Connection Dialog
 *
 * Arquitectura:
 * - UI creada con Qt Designer (mainwindow.ui)
 * - Backend: ScanController (JTAG core logic)
 * - Threading: ScanWorker ejecuta polling en thread separado
 * - Señales/Slots: Comunicación asíncrona thread-safe
 */

// Qt Headers
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
#include <QMetaType>
#include <QSettings>

// Standard Library
#include <iostream>
#include <iomanip>
#include <cmath>

// Backend Headers
#include "../controller/ScanController.h"
#include "../hal/JtagProtocol.h"
#include "ConnectionDialog.h"
#include "ChainExamineDialog.h"
#include "NewProjectWizard.h"
#include "SettingsDialog.h"

/**
 * @brief Constructor de la ventana principal
 *
 * Inicializa todos los componentes de la interfaz gráfica y backend:
 * 1. Carga el diseño UI desde mainwindow.ui
 * 2. Registra tipos Qt personalizados para señales cross-thread
 * 3. Inicializa la UI (título, iconos, estado inicial)
 * 4. Configura vistas gráficas (ChipVisualizer, waveform)
 * 5. Configura tablas de pines y watch panel
 * 6. Configura la barra de herramientas personalizada
 * 7. Crea el ScanController (backend JTAG)
 * 8. Conecta señales/slots entre UI y backend
 * 9. Establece estado inicial (controles deshabilitados hasta conectar)
 *
 * @param parent Widget padre (nullptr por defecto para ventana independiente)
 */
MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
    , ui(new Ui::MainWindow)
    , scanController(nullptr)
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
    , currentZoom(1.0)
    , isAdapterConnected(false)
    , isDeviceDetected(false)
    , isDeviceInitialized(false)
    , currentJTAGMode(JTAGMode::SAMPLE)
    , isCapturing(false)
    , waveformTimebase(1.0)
    , isRedrawing(false)
    , isAutoScrollEnabled(true)
    , m_waveformRenderTimer(nullptr)
    , m_waveformNeedsRedraw(false)
{
    // Cargar diseño UI desde mainwindow.ui
    ui->setupUi(this);

    // Registrar tipos personalizados para señales Qt cross-thread
    // Necesario para comunicación MainWindow <-> ScanWorker (thread separado)
    qRegisterMetaType<std::vector<JTAG::PinLevel>>("std::vector<JTAG::PinLevel>");
    qRegisterMetaType<JTAG::PinLevel>("JTAG::PinLevel");

    // Secuencia de inicialización
    initializeUI();           // Configuración inicial: título, iconos, etc.
    setupGraphicsViews();     // Chip visualizer, waveform viewer
    setupTables();            // Tabla de pines principal
    setupToolbar();           // Barra de herramientas personalizada

    // Crear Control Panel ANTES de setupConnections() para conectar señales
    controlPanel = new ControlPanelWidget(this);

    // Reemplazar widget placeholder en dockWatch con control panel real
    QWidget* oldWidget = ui->dockWatch->widget();
    ui->dockWatch->setWidget(controlPanel);
    delete oldWidget;

    // Ocultar Watch panel por defecto (se muestra al agregar pines)
    ui->dockWatch->setVisible(false);
    ui->actionWatch->setChecked(false);

    setupBackend();           // Crear ScanController (backend JTAG)
    setupConnections();       // Conectar señales/slots UI <-> Backend

    // ===== RENDER THROTTLING: Configurar timer @ 30 FPS =====
    // Soluciona Event Loop Starvation con polling ultra-rápido (1ms = 1000 Hz)
    // Captura de datos: hasta 1000 Hz
    // Renderizado UI: limitado a 30 FPS (33ms interval)
    m_waveformRenderTimer = new QTimer(this);
    m_waveformRenderTimer->setInterval(33);  // 30 FPS
    connect(m_waveformRenderTimer, &QTimer::timeout, this, [this]() {
        // Solo redibujar si hay cambios pendientes y el dock es visible
        if (m_waveformNeedsRedraw && ui->dockWaveform->isVisible()) {
            redrawWaveform();
            m_waveformNeedsRedraw = false;
        }
    });
    // Timer se inicia automáticamente cuando se añaden señales al waveform
    // =========================================================

    updateWindowTitle();
    enableControlsAfterConnection(false);  // Deshabilitar hasta conectar adaptador

    // Mostrar Waveform dock por defecto con tamaño razonable
    ui->dockWaveform->setVisible(true);
    ui->dockWaveform->resize(1200, 300);
    ui->actionWaveform->setChecked(true);

    // Load saved window state (geometry, docks, column widths)
    loadWindowState();

    // Load performance settings
    QSettings settings("TopJTAG", "BoundaryScanner");
    currentPollInterval = settings.value("performance/pollInterval", 100).toInt();
    currentSampleDecimation = settings.value("performance/sampleDecimation", 1).toInt();
}

/**
 * @brief Destructor de la ventana principal
 *
 * Limpia recursos y detiene operaciones en progreso:
 * - Detiene el polling de pines si está activo
 * - Libera escenas gráficas (waveform, timeline)
 * - Libera interfaz UI
 */
MainWindow::~MainWindow()
{
    // Save window state before closing
    saveWindowState();

    // Detener polling si está activo
    if (scanController && isCapturing) {
        scanController->stopPolling();
    }
    delete waveformScene;
    delete timelineScene;
    delete ui;
}

/**
 * @brief Inicializa la interfaz de usuario
 *
 * Configura parámetros iniciales de la ventana:
 * - Tamaño por defecto (1200x800)
 * - Estado inicial de la barra de estado
 */
void MainWindow::initializeUI()
{
    resize(1200, 800);
    updateStatusBar("Ready");
}

/**
 * @brief Configura el backend JTAG (ScanController)
 *
 * Crea e inicializa el controlador principal del sistema JTAG:
 * 1. Crea instancia de ScanController
 * 2. Conecta señales del ScanController con slots de MainWindow:
 *    - pinsDataReady: Actualiza UI cuando hay nuevos datos de pines
 *    - errorOccurred: Muestra errores del backend al usuario
 *
 * Nota: El polling se maneja en ScanWorker (thread separado)
 */
void MainWindow::setupBackend()
{
    scanController = std::make_unique<JTAG::ScanController>();

    if (!scanController) {
        QMessageBox::critical(this, "Initialization Error",
            "Failed to create ScanController");
        return;
    }

    // Conectar señales del ScanController al MainWindow para comunicación asíncrona
    connect(scanController.get(), &JTAG::ScanController::pinsDataReady,
            this, &MainWindow::onPinsDataReady);
    connect(scanController.get(), &JTAG::ScanController::errorOccurred,
            this, &MainWindow::onScanError);
}

/**
 * @brief Configura las vistas gráficas (chip visualizer y waveform viewer)
 *
 * Inicializa los componentes de visualización:
 * 1. ChipVisualizer: Vista gráfica del chip con representación de pines
 * 2. Waveform Viewer: Visualizador de formas de onda digitales
 * 3. Timeline: Línea de tiempo para el waveform
 *
 * El waveform viewer consta de tres partes:
 * - Timeline (arriba): Marcadores de tiempo
 * - Nombres (izquierda): Nombres de señales observadas
 * - Waveforms (derecha): Formas de onda propiamente dichas
 */
void MainWindow::setupGraphicsViews()
{
    // Crear y configurar ChipVisualizer (reemplaza QGraphicsView placeholder)
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
                    QScrollBar* hScrollBar = ui->graphicsViewWaveform->horizontalScrollBar();
                    int maxScroll = hScrollBar->maximum();

                    // Si está cerca del final (últimos 100px), reactivar auto-scroll
                    // Si no, deshabilitar para permitir navegación histórica
                    if (maxScroll - value < 100) {
                        isAutoScrollEnabled = true;
                    } else {
                        isAutoScrollEnabled = false;
                    }

                    timelineView->horizontalScrollBar()->setValue(value);
                    // Throttling: marcar dirty flag en lugar de redraw síncrono
                    m_waveformNeedsRedraw = true;
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

    // Conectar señales de tabla de pines
    connect(ui->tableWidgetPins, &QTableWidget::itemChanged,
            this, &MainWindow::onPinTableItemChanged);
    connect(ui->tableWidgetPins->selectionModel(), &QItemSelectionModel::selectionChanged,
            this, &MainWindow::onPinTableSelectionChanged);

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
    QStringList zoomLevels = { "25%", "50%", "75%", "100%", "125%", "150%", "200%", "300%", "400%" };
    zoomComboBox->addItems(zoomLevels);

    // Buscamos dónde quedó el "100%" para seleccionarlo por defecto
    int defaultIndex = zoomComboBox->findText("100%");
    if (defaultIndex != -1) {
        zoomComboBox->setCurrentIndex(defaultIndex);
    }

    zoomComboBox->setMinimumWidth(120);
    zoomComboBox->setMaximumWidth(120);

    // Agrandar fuente para mejor visibilidad
    QFont zoomFont = zoomComboBox->font();
    zoomFont.setPointSize(10);
    zoomComboBox->setFont(zoomFont);
    
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
    
    connect(zoomComboBox, QOverload<int>::of(&QComboBox::activated),
            this, [this](int) { onZoom(); });
    
   

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
    connect(ui->actionZoom_Menu, &QAction::triggered, this, &MainWindow::onZoom);
    connect(ui->actionSettings, &QAction::triggered, this, &MainWindow::onSettings);

    // Scan menu connections
    connect(ui->actionJTAG_Connection, &QAction::triggered, this, &MainWindow::onJTAGConnection);
    connect(ui->actionExamine_Chain, &QAction::triggered, this, &MainWindow::onExamineChain);
    connect(ui->actionRun, &QAction::triggered, this, &MainWindow::onRun);
    connect(ui->actionReset, &QAction::triggered, this, &MainWindow::onReset);
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

/**
 * @brief Abre el asistente de nuevo proyecto (New Project Wizard)
 *
 * El wizard guía al usuario en la configuración inicial del proyecto:
 * 1. Si no hay dispositivo detectado, intenta leer el IDCODE automáticamente
 * 2. Muestra diálogo del wizard con el IDCODE detectado
 * 3. Permite configurar:
 *    - Tipo de package (Edge Pins / Center Pins)
 *    - Dimensiones visuales del chip
 * 4. Configura el ChipVisualizer con los parámetros elegidos
 * 5. Renderiza un placeholder del chip con el IDCODE
 * 6. Redirige automáticamente a carga de BSDL después de 100ms
 *
 * Flujo típico:
 * - Connect > Scan/JTAG Connection (conecta adaptador)
 * - File > New Project Wizard (este método)
 * - [Wizard muestra IDCODE y config visual]
 * - [Usuario acepta] -> Se abre File Dialog para cargar BSDL
 *
 * @note Si no puede leer IDCODE, muestra advertencia y aborta
 * @note El wizard lee el IDCODE automáticamente si no estaba detectado
 */
void MainWindow::onNewProjectWizard()
{
    if (!scanController) {
        QMessageBox::warning(this, "No Controller", "ScanController not initialized");
        return;
    }

    // Si no hay dispositivo detectado, intentar leer el IDCODE automáticamente
    uint32_t idcode = 0;
    if (!isDeviceDetected) {
        idcode = scanController->detectDevice();
        if (idcode != 0) {
            isDeviceDetected = true;
            updateStatusBar(QString("Device detected: IDCODE 0x%1").arg(idcode, 8, 16, QChar('0')));
        } else {
            QMessageBox::warning(this, "No Device Detected",
                "Failed to read IDCODE from device.\n\n"
                "Please check:\n"
                "- JTAG adapter is connected\n"
                "- Target device is powered on\n"
                "- JTAG connections are correct");
            return;
        }
    } else {
        idcode = scanController->getIDCODE();
    }

    // Proceder con el wizard
    NewProjectWizard wizard(idcode, this);
    if (wizard.exec() == QDialog::Accepted) {

        // 1. Obtener configuración del Wizard
        auto packageType = wizard.getPackageType();
        int horizontalPins = wizard.getHorizontalPins();
        int verticalPins = wizard.getVerticalPins();
        customDeviceName = wizard.getDeviceName();

        qDebug() << "[MainWindow] Wizard config: packageType ="
                 << (packageType == PackageTypePage::PackageType::EDGE_PINS ? "EDGE_PINS" : "CENTER_PINS")
                 << ", horizontal =" << horizontalPins
                 << ", vertical =" << verticalPins;

        // 2. Calcular dimensiones del chip según tipo y proporción de pines
        double chipWidth, chipHeight;

        if (packageType == PackageTypePage::PackageType::CENTER_PINS) {
            // BGA/CENTER → Siempre cuadrado
            chipWidth = 400.0;
            chipHeight = 400.0;
        }
        else {
            // EDGE_PINS → Calcular según proporción de pines

            // Si ambos son 0 (auto), hacer cuadrado
            if (horizontalPins == 0 && verticalPins == 0) {
                chipWidth = 400.0;
                chipHeight = 400.0;
            }
            else {
                // Calcular proporción
                double ratio;

                if (horizontalPins == 0 || verticalPins == 0) {
                    // Solo uno especificado → cuadrado
                    ratio = 1.0;
                }
                else {
                    ratio = static_cast<double>(horizontalPins) / static_cast<double>(verticalPins);
                }

                // Si la proporción es extrema (>5 o <0.2), hacer cuadrado
                if (ratio > 5.0 || ratio < 0.2) {
                    chipWidth = 400.0;
                    chipHeight = 400.0;
                    qDebug() << "[MainWindow] Pin ratio too extreme (" << ratio
                             << "), using square chip";
                }
                else {
                    // Proporción razonable → aplicarla
                    chipHeight = 400.0;
                    chipWidth = chipHeight * ratio;
                    qDebug() << "[MainWindow] Chip dimensions calculated from pin ratio:"
                             << chipWidth << "x" << chipHeight
                             << "(ratio:" << ratio << ")";
                }
            }
        }

        qDebug() << "[MainWindow] Final chip dimensions:" << chipWidth << "x" << chipHeight;

        // 3. Configurar Visualizador
        if (packageType == PackageTypePage::PackageType::EDGE_PINS) {
            chipVisualizer->setPackageType("EDGE");
        }
        else {
            chipVisualizer->setPackageType("CENTER");
        }

        // Establecer dimensiones y dibujar placeholder INMEDIATAMENTE
        chipVisualizer->setCustomDimensions(chipWidth, chipHeight);
        qDebug() << "[MainWindow] Dimensions set, rendering placeholder...";
        chipVisualizer->renderPlaceholder(idcode);

        updateStatusBar("Project settings updated. Waiting for BSDL...");

        // 3. Redirigir AUTOMÁTICAMENTE a cargar BSDL
        // Usamos un QTimer::singleShot con 0ms para dejar que el UI se refresque
        // y el wizard se cierre visualmente antes de abrir el explorador de archivos.
        QTimer::singleShot(100, this, [this]() {
            onDeviceBSDLFile(); // <--- Redirección automática
            });
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

    // ===== OPTIMIZACIÓN: Reiniciar timer cuando se muestra waveform =====
    if (checked && !waveformSignals.empty() && !m_waveformRenderTimer->isActive()) {
        m_waveformRenderTimer->start();
        m_waveformNeedsRedraw = true;  // Redraw inmediato al mostrar
    } else if (!checked) {
        m_waveformRenderTimer->stop();  // Pausar cuando se oculta
    }
    // ====================================================================
}

void MainWindow::onZoom()
{
    QString zoomText = zoomComboBox->currentText();
    zoomText.remove('%');
    currentZoom = zoomText.toDouble() / 100.0;

    chipVisualizer->resetTransform();
    chipVisualizer->scale(currentZoom, currentZoom);

    updateStatusBar(QString("Zoom: %1%").arg(zoomText));
}

void MainWindow::onSettings()
{
    SettingsDialog dialog(this);
    dialog.setPollingInterval(currentPollInterval);
    dialog.setSampleDecimation(currentSampleDecimation);

    connect(&dialog, &SettingsDialog::pollingIntervalChanged,
            this, &MainWindow::onPollingIntervalChanged);
    connect(&dialog, &SettingsDialog::sampleDecimationChanged,
            this, &MainWindow::onSampleDecimationChanged);

    dialog.exec();
}

void MainWindow::onPollingIntervalChanged(int ms)
{
    currentPollInterval = ms;

    // Apply to scan controller if available
    if (scanController) {
        scanController->setPollInterval(ms);
    }

    // Save to settings
    QSettings settings("TopJTAG", "BoundaryScanner");
    settings.setValue("performance/pollInterval", ms);

    updateStatusBar(QString("Polling interval: %1 ms").arg(ms));
}

void MainWindow::onSampleDecimationChanged(int decimation)
{
    currentSampleDecimation = decimation;
    sampleCounter = 0;  // Reset counter

    // Save to settings
    QSettings settings("TopJTAG", "BoundaryScanner");
    settings.setValue("performance/sampleDecimation", decimation);

    QString msg = (decimation == 1)
        ? "Capturing all samples"
        : QString("Capturing 1 of every %1 samples").arg(decimation);
    updateStatusBar(msg);
}

/**
 * @brief Conecta un adaptador JTAG
 *
 * Flujo de conexión:
 * 1. Detecta adaptadores JTAG disponibles (Mock, J-Link, Pico)
 * 2. Muestra diálogo de selección si hay múltiples adaptadores
 * 3. Conecta el adaptador seleccionado
 * 4. Lee el IDCODE del dispositivo target
 * 5. Habilita controles de la UI para operaciones JTAG
 *
 * Actualiza el estado de la aplicación:
 * - isAdapterConnected = true
 * - isDeviceDetected = true si IDCODE válido
 *
 * Después de conectar, el usuario puede:
 * - Cargar archivo BSDL (File > Load BSDL)
 * - Examinar la cadena JTAG (Scan > Examine Chain)
 * - Usar el New Project Wizard
 *
 * @note Si solo hay un adaptador, se conecta automáticamente
 * @note Si el IDCODE no es válido (0x00000000 o 0xFFFFFFFF), muestra advertencia
 */
void MainWindow::onJTAGConnection()
{
    if (!scanController) {
        QMessageBox::critical(this, "Error", "ScanController not initialized");
        return;
    }

    // Detectar adaptadores JTAG disponibles (Mock, J-Link, Pico, etc.)
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

        // RESETEAR TODO ANTES DE CONECTAR NUEVA SONDA
        std::cout << "[MainWindow] Resetting state before connecting new adapter\n";

        // Detener captura si está activa
        if (isCapturing) {
            isCapturing = false;
            ui->actionRun->setText("Run");
            if (scanController) {
                scanController->stopPolling();
            }
        }

        // Limpiar estado
        isDeviceDetected = false;
        isDeviceInitialized = false;

        // Limpiar controles
        ui->comboBoxDevice->clear();
        ui->tableWidgetPins->setRowCount(0);

        if (controlPanel) {
            controlPanel->removeAllPins();
            ui->dockWatch->setVisible(false);
        }

        // Resetear visualización del chip
        if (chipVisualizer) {
            chipVisualizer->scene()->clear();
            chipVisualizer->update();
        }

        // Resetear modo JTAG a SAMPLE
        if (radioSample) {
            radioSample->setChecked(true);
        }
        currentJTAGMode = JTAGMode::SAMPLE;

        // Conectar nueva sonda
        if (scanController->connectAdapter(descriptor, clockSpeed)) {
            isAdapterConnected = true;

            QString adapterName = QString::fromStdString(descriptor.name);
            QString serialInfo = QString::fromStdString(descriptor.serialNumber);

            updateStatusBar(QString("Connected to %1 (%2) @ %3 Hz")
                .arg(adapterName)
                .arg(serialInfo)
                .arg(clockSpeed));

            enableControlsAfterConnection(true);

            // Detectar dispositivo y abrir wizard automáticamente
            QTimer::singleShot(300, this, [this]() {
                uint32_t idcode = scanController->detectDevice();

                if (idcode != 0 && idcode != 0xFFFFFFFF) {
                    isDeviceDetected = true;

                    // Actualizar combo
                    ui->comboBoxDevice->clear();
                    ui->comboBoxDevice->addItem(
                        QString("Device 0x%1").arg(idcode, 8, 16, QChar('0')));

                    updateStatusBar(QString("Device detected - IDCODE: 0x%1")
                        .arg(idcode, 8, 16, QChar('0')));

                    // Abrir New Project Wizard automáticamente
                    QTimer::singleShot(200, this, &MainWindow::onNewProjectWizard);
                } else {
                    QMessageBox::warning(this, "No Device Detected",
                        "Failed to read IDCODE from device.\n\n"
                        "Please check:\n"
                        "- Target device is powered on\n"
                        "- JTAG connections are correct\n"
                        "- Target is not held in reset");
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

void MainWindow::onReset()
{
    if (!scanController) {
        QMessageBox::warning(this, "No Controller", "ScanController not initialized");
        return;
    }

    // Confirmar acción
    QMessageBox::StandardButton reply = QMessageBox::question(
        this,
        "Reset",
        "This will unload the BSDL file and clear device data.\n"
        "The adapter will remain connected.\n\n"
        "Do you want to continue?",
        QMessageBox::Yes | QMessageBox::No
    );

    if (reply == QMessageBox::Yes) {
        // IMPORTANTE: Detener el worker ANTES de descargar el BSDL
        if (isCapturing) {
            scanController->stopPolling();
            isCapturing = false;
            ui->actionRun->setText("Run");
        }

        // Llamar al nuevo método que solo descarga el BSDL y limpia el target
        // pero mantiene la sonda conectada
        scanController->unloadBSDL();

        // Actualizar estado de la UI
        // isAdapterConnected - MANTENER true (sonda sigue conectada)
        // isDeviceDetected - Poner false (el IDCODE del target se ha limpiado)
        // isDeviceInitialized - Poner false (el BSDL está descargado)
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

        // Habilitar solo controles básicos (mantener conexión activa)
        enableControlsAfterConnection(true);  // CAMBIO: true en lugar de false

        // Resetear modo JTAG a SAMPLE (solo UI, sin disparar el worker)
        if (radioSample) {
            radioSample->blockSignals(true);
            radioSample->setChecked(true);
            radioSample->blockSignals(false);
        }
        currentJTAGMode = JTAGMode::SAMPLE;

        updateStatusBar("Reset: BSDL unloaded, adapter still connected");

        QMessageBox::information(this, "Reset Complete",
            "BSDL unloaded successfully.\n"
            "Adapter remains connected.\n\n"
            "You can now load a new BSDL file or examine the chain again.");
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
        "This will reset the JTAG state machine by sending:\n"
        "- 5 TMS=1 clocks (to Test-Logic-Reset)\n"
        "- 1 TMS=0 clock (to Run-Test/Idle)\n\n"
        "Use this if the TAP controller is in an unknown state.\n\n"
        "Do you want to continue?",
        QMessageBox::Yes | QMessageBox::No
    );

    if (reply == QMessageBox::Yes) {
        // 1. Detener worker si está corriendo
        if (isCapturing) {
            scanController->stopPolling();
            isCapturing = false;
            ui->actionRun->setText("Run");
            qDebug() << "[MainWindow] Worker stopped for JTAG Reset";
        }

        // 2. Ejecutar reset JTAG (secuencia TMS: 5×1 + 1×0)
        // Esto deja el TAP en Run-Test/Idle sin instrucción cargada
        if (!scanController->resetJTAGStateMachine()) {
            updateStatusBar("JTAG Reset failed - check adapter connection");
            qDebug() << "[MainWindow] JTAG Reset FAILED";
            return;
        }

        // 3. Desmarcar todos los radio buttons para indicar que no hay modo activo
        if (jtagModeButtonGroup) {
            jtagModeButtonGroup->setExclusive(false);
            if (radioSample) radioSample->setChecked(false);
            if (radioSampleSingleShot) radioSampleSingleShot->setChecked(false);
            if (radioExtest) radioExtest->setChecked(false);
            if (radioIntest) radioIntest->setChecked(false);
            if (radioBypass) radioBypass->setChecked(false);
            jtagModeButtonGroup->setExclusive(true);
        }

        // 4. Estado final: TAP en IDLE, worker parado, sin instrucción cargada
        updateStatusBar("JTAG TAP reset to RUN_TEST_IDLE - Select mode to continue");
        qDebug() << "[MainWindow] JTAG Reset complete - TAP in IDLE, no instruction loaded";
    }
}

/**
 * @brief Selector de instrucción JTAG (no implementado)
 *
 * Placeholder para diálogo futuro de selección manual de instrucciones JTAG.
 * Actualmente las instrucciones se cambian mediante los botones SAMPLE/EXTEST.
 */
void MainWindow::onDeviceInstruction()
{
    QMessageBox::information(this, "Device Instruction", "Device Instruction dialog - To be implemented");
}

/**
 * @brief Carga un archivo BSDL (Boundary Scan Description Language)
 *
 * Flujo de carga de BSDL:
 * 1. Verifica que haya un adaptador JTAG conectado
 * 2. Muestra diálogo para seleccionar archivo .bsd/.bsdl
 * 3. Carga y parsea el archivo BSDL mediante ScanController
 * 4. Inicializa el dispositivo (configura BSR, entra en modo SAMPLE)
 * 5. Actualiza la tabla de pines con información del BSDL
 * 6. Renderiza el chip en ChipVisualizer con layout del BSDL
 * 7. Inicia el polling automático de pines
 *
 * El archivo BSDL contiene:
 * - IDCODE del dispositivo
 * - Longitud del Boundary Scan Register (BSR)
 * - Definición de cada pin (nombre, tipo, celda BSR)
 * - Instrucciones JTAG soportadas (SAMPLE, EXTEST, etc.)
 * - Información del package (pinout físico)
 *
 * Después de cargar el BSDL exitosamente:
 * - isDeviceInitialized = true
 * - Se habilitan controles de pin (Set 0, Set 1, Toggle, etc.)
 * - Se puede cambiar entre modos SAMPLE y EXTEST
 * - Se puede observar el estado de pines en tiempo real
 *
 * @note Requiere adaptador conectado (isAdapterConnected = true)
 * @note Soporta rutas con caracteres Unicode en Windows
 */
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
        // Convertir QString a std::filesystem::path para soporte Unicode completo
        #ifdef _WIN32
            std::filesystem::path bsdlPath(fileName.toStdWString());
        #else
            std::filesystem::path bsdlPath(fileName.toStdString());
        #endif

        if (scanController->loadBSDL(bsdlPath)) {
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
    if (!isEditingModeActive()) return;

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
    if (!isEditingModeActive()) return;

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

    if (!isEditingModeActive()) return;

    // Default safe value: HIGH_Z (tristate)
    auto pinNames = scanController->getPinList();

    for (const auto& pinName : pinNames) {
        scanController->setPin(pinName, JTAG::PinLevel::HIGH_Z);
    }

    scanController->applyChanges();
    updateStatusBar(QString("Set %1 pins to safe state (HIGH_Z)").arg(pinNames.size()));
    updatePinsTable();
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

    // ===== OPTIMIZACIÓN: Pausar render timer cuando waveform oculto =====
    // Ahorra CPU al no redibujar gráficos invisibles
    m_waveformRenderTimer->stop();
    // ====================================================================
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

    if (!scanController || !scanController->getDeviceModel()) {
        updateStatusBar("No device model loaded");
        return;
    }

    // BUG FIX 1: Detectar si es la primera vez que se añaden señales
    bool wasEmpty = waveformSignals.empty();

    for (int row : rows) {
        QTableWidgetItem *sourceItem = ui->tableWidgetPins->item(row, 0);
        if (sourceItem) {
            std::string pinName = sourceItem->text().toStdString();

            // Verificar que no exista ya (ahora con WaveformSignalInfo)
            auto it = std::find_if(waveformSignals.begin(), waveformSignals.end(),
                [&pinName](const WaveformSignalInfo& sig) { return sig.name == pinName; });

            if (it == waveformSignals.end()) {
                // ===== OPTIMIZACIÓN: Cachear índice BSR directo UNA VEZ =====
                // Determinar qué índice del vector BSR corresponde a este pin
                // (misma lógica que ScanController::getPin)
                auto pinInfo = scanController->getDeviceModel()->getPinInfo(pinName);
                if (pinInfo) {
                    WaveformSignalInfo sigInfo;
                    sigInfo.name = pinName;

                    // Calcular índice directo en el vector BSR
                    // Prioridad: inputCell > outputCell
                    if (pinInfo->inputCell != -1) {
                        sigInfo.dataIndex = pinInfo->inputCell;
                    } else if (pinInfo->outputCell != -1) {
                        sigInfo.dataIndex = pinInfo->outputCell;
                    } else {
                        sigInfo.dataIndex = -1;  // Pin sin celdas JTAG (no se puede monitorear)
                    }

                    waveformSignals.push_back(sigInfo);
                    waveformBuffer[pinName].clear();
                }
                // ===========================================================
            }
        }
    }

    updateStatusBar(QString("Added %1 signal(s) to Waveform").arg(rows.size()));

    // ===== RENDER THROTTLING: Iniciar timer cuando se añade primera señal =====
    if (!waveformSignals.empty()) {
        // Marcar dirty flag para redraw inmediato
        m_waveformNeedsRedraw = true;

        // Iniciar timer @ 30 FPS si no está corriendo
        if (!m_waveformRenderTimer->isActive()) {
            m_waveformRenderTimer->start();
        }

        // Redraw síncrono inicial para mostrar grid/nombres inmediatamente
        // (solo cuando se añade primera señal, evita pantalla en blanco)
        if (wasEmpty) {
            redrawWaveform();
            m_waveformNeedsRedraw = false;
        }
    }
    // ===========================================================================
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
    for (const auto& sigInfo : waveformSignals) {
        listWidget->addItem(QString::fromStdString(sigInfo.name));
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

        // Eliminar de waveformSignals (ahora con WaveformSignalInfo)
        for (const auto& pin : removedPins) {
            waveformSignals.erase(
                std::remove_if(waveformSignals.begin(), waveformSignals.end(),
                    [&pin](const WaveformSignalInfo& sig) { return sig.name == pin; }),
                waveformSignals.end());
            waveformBuffer.erase(pin);
        }

        updateStatusBar(QString("Removed %1 signal(s)").arg(removedPins.size()));

        // ===== RENDER THROTTLING: Detener timer si no quedan señales =====
        if (waveformSignals.empty()) {
            m_waveformRenderTimer->stop();
            m_waveformNeedsRedraw = false;
        }
        // Marcar dirty flag para redraw
        m_waveformNeedsRedraw = true;
        // =================================================================
    }
}

void MainWindow::onWaveformRemoveAll()
{
    waveformSignals.clear();
    waveformBuffer.clear();
    updateStatusBar("Waveform signals cleared");

    // ===== RENDER THROTTLING: Detener timer cuando no hay señales =====
    m_waveformRenderTimer->stop();
    m_waveformNeedsRedraw = false;
    // ==================================================================

    // Redibujar waveform (vacío) - síncrono para limpiar pantalla inmediatamente
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
    // Throttling: marcar dirty flag para redraw
    m_waveformNeedsRedraw = true;
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
    // Throttling: marcar dirty flag para redraw
    m_waveformNeedsRedraw = true;
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
    // Throttling: marcar dirty flag para redraw
    m_waveformNeedsRedraw = true;
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

    // ===== OPTIMIZACIÓN: Congelar actualizaciones visuales =====
    // Con 200+ pines, Qt hace 200+ repaints síncronos sin esto
    // Esto reduce tiempo de actualización de ~200ms a ~20ms
    ui->tableWidgetPins->setUpdatesEnabled(false);
    if (chipVisualizer) {
        chipVisualizer->setUpdatesEnabled(false);
    }
    // ===========================================================

    // ===== OPTIMIZACIÓN: Obtener todos los pines UNA VEZ =====
    // En lugar de llamar getPinType(), getPinNumber() etc. repetidamente,
    // obtenemos todo el vector una vez y creamos hash map O(1)
    const auto& allPins = scanController->getDeviceModel()->getAllPins();
    QHash<QString, const JTAG::PinInfo*> pinInfoCache;
    pinInfoCache.reserve(allPins.size());
    for (const auto& pin : allPins) {
        pinInfoCache[QString::fromStdString(pin.name)] = &pin;
    }
    // =========================================================

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

            // ===== OPTIMIZACIÓN: Usar cache en lugar de llamadas =====
            const JTAG::PinInfo* pinInfo = pinInfoCache.value(qPinName, nullptr);
            if (pinInfo) {
                // Col 1: Pin #
                ui->tableWidgetPins->setItem(row, 1, new QTableWidgetItem(QString::fromStdString(pinInfo->pinNumber)));

                // Col 2: Port
                ui->tableWidgetPins->setItem(row, 2, new QTableWidgetItem(QString::fromStdString(pinInfo->port)));

                // Col 3: I/O Value (Inicial)
                ui->tableWidgetPins->setItem(row, 3, new QTableWidgetItem("?"));

                // Col 4: Type
                ui->tableWidgetPins->setItem(row, 4, new QTableWidgetItem(QString::fromStdString(pinInfo->type)));
            } else {
                // Fallback si no está en cache (no debería pasar)
                ui->tableWidgetPins->setItem(row, 1, new QTableWidgetItem(""));
                ui->tableWidgetPins->setItem(row, 2, new QTableWidgetItem(""));
                ui->tableWidgetPins->setItem(row, 3, new QTableWidgetItem("?"));
                ui->tableWidgetPins->setItem(row, 4, new QTableWidgetItem(""));
            }
            // =========================================================
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

        // ===== OPTIMIZACIÓN: Obtener tipo del cache O(1) =====
        const JTAG::PinInfo* pinInfo = pinInfoCache.value(realName, nullptr);
        if (!pinInfo) continue; // Pin no encontrado (no debería pasar)

        QString type = QString::fromStdString(pinInfo->type);
        // =====================================================

        // 3. Leer estado del pin
        auto level = scanController->getPin(pinName);

        if (level.has_value()) {
            QString valueStr;
            VisualPinState visualState;

            // Verificar si es un pin LINKAGE (no controlable)
            if (type.toLower() == "linkage") {
                valueStr = "-";
                visualState = VisualPinState::LINKAGE;
            }
            else {
                // Pin normal - asignar según nivel
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
                    // HIGH_Z es un estado válido - usar amarillo (OSCILLATING)
                    visualState = VisualPinState::OSCILLATING;
                    break;
                }
            }

            // 4. Actualizar la celda de VALOR (Columna 3)
            QTableWidgetItem* valueItem = ui->tableWidgetPins->item(row, 3);
            if (valueItem) {
                // ===== OPTIMIZACIÓN: Diffing - solo actualizar si cambió =====
                if (valueItem->text() != valueStr) {
                    valueItem->setText(valueStr);
                }
                // =============================================================

                // --- Lógica de Edición (EXTEST) ---
                // Permitir editar si es EXTEST y es una salida (incluyendo output2 del hack)
                // NOTA: Los pines LINKAGE nunca son editables
                QString typeLower = type.toLower();
                bool isEditable = (currentJTAGMode == JTAGMode::EXTEST) &&
                    (typeLower == "output" || typeLower == "inout" || typeLower == "output2") &&
                    (typeLower != "linkage");

                // ===== OPTIMIZACIÓN: Diffing de color también =====
                QColor targetColor = isEditable ? QColor(255, 255, 200) : Qt::white;
                if (valueItem->background().color() != targetColor) {
                    valueItem->setBackground(targetColor);
                }
                // ==================================================

                // Flags siempre hay que actualizar (no hay comparación eficiente)
                if (isEditable) {
                    valueItem->setFlags(valueItem->flags() | Qt::ItemIsEditable);
                }
                else {
                    valueItem->setFlags(valueItem->flags() & ~Qt::ItemIsEditable);
                }
            }

            // 5. Actualizar visualizador del chip (solo si es visible)
            // ===== OPTIMIZACIÓN: No actualizar si está oculto =====
            if (chipVisualizer && chipVisualizer->isVisible()) {
                chipVisualizer->updatePinState(realName, visualState);
            }
            // ======================================================
        }
        else {
            // Pin no tiene valor - Verificar si es LINKAGE o simplemente no accesible
            // ===== OPTIMIZACIÓN: Reutilizar 'type' del cache (ya obtenido arriba) =====
            // No necesitamos llamar getPinType() de nuevo, 'type' ya está disponible

            QTableWidgetItem* valueItem = ui->tableWidgetPins->item(row, 3);
            if (valueItem) {
                if (type.toLower() == "linkage") {
                    // Pin LINKAGE (VCC, GND, NC) - no controlable vía JTAG
                    valueItem->setText("-");
                    valueItem->setFlags(valueItem->flags() & ~Qt::ItemIsEditable);
                    valueItem->setBackground(Qt::darkGray);

                    // Mantener estado LINKAGE (negro) en visualizador (solo si visible)
                    if (chipVisualizer && chipVisualizer->isVisible()) {
                        chipVisualizer->updatePinState(realName, VisualPinState::LINKAGE);
                    }
                } else {
                    // Pin normal sin valor (no accesible)
                    valueItem->setText("?");
                    valueItem->setFlags(valueItem->flags() & ~Qt::ItemIsEditable);
                    valueItem->setBackground(Qt::lightGray);

                    // Actualizar visualizador como UNKNOWN (gris, solo si visible)
                    if (chipVisualizer && chipVisualizer->isVisible()) {
                        chipVisualizer->updatePinState(realName, VisualPinState::UNKNOWN);
                    }
                }
            }
        }
    }

    // ===== OPTIMIZACIÓN: Descongelar actualizaciones visuales =====
    // Forzar un solo repaint al final (en orden inverso - LIFO)
    if (chipVisualizer) {
        chipVisualizer->setUpdatesEnabled(true);
    }
    ui->tableWidgetPins->setUpdatesEnabled(true);
    // ==============================================================

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

    // Render the chip visualization using the device model with custom name
    chipVisualizer->renderFromDeviceModel(*deviceModel, customDeviceName);
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

void MainWindow::captureWaveformSample(const std::vector<JTAG::PinLevel>& currentPins)
{
    // ==================== PUNTO DE INTEGRACIÓN 13 ====================
    if (waveformSignals.empty()) return;

    double currentTime = captureTimer.elapsed() / 1000.0; // Convert ms to seconds

    // ===== OPTIMIZACIÓN MÁXIMA: Acceso directo por índice =====
    // Elimina TODAS las búsquedas, hash lookups y llamadas de función
    // Antes: 20 señales × 1000Hz × getPin() = 20,000 llamadas/seg
    // Ahora: 20 señales × 1000Hz × acceso[i] = acceso directo a memoria
    for (const auto& sigInfo : waveformSignals) {
        // Validación de seguridad (bounds checking)
        if (sigInfo.dataIndex >= 0 && sigInfo.dataIndex < static_cast<int>(currentPins.size())) {
            // *** ACCESO DIRECTO A MEMORIA - INSTANTÁNEO ***
            JTAG::PinLevel level = currentPins[sigInfo.dataIndex];

            // Add sample to buffer
            waveformBuffer[sigInfo.name].push_back({currentTime, level});

            // Maintain circular buffer
            if (waveformBuffer[sigInfo.name].size() > MAX_WAVEFORM_SAMPLES) {
                waveformBuffer[sigInfo.name].pop_front();
            }
        }
    }
    // ============================================================

    // ===== RENDER THROTTLING: Marcar dirty flag =====
    // NO llamar redrawWaveform() aquí (bloqueante, causa Event Loop Starvation)
    // El timer @ 30 FPS se encarga del redraw de forma asíncrona
    m_waveformNeedsRedraw = true;

    // Asegurar que el timer está corriendo (auto-start cuando hay señales)
    if (!m_waveformRenderTimer->isActive() && !waveformSignals.empty()) {
        m_waveformRenderTimer->start();
    }
    // ================================================
}

void MainWindow::redrawWaveform()
{
    // ===== OPTIMIZACIÓN: No dibujar si no es visible =====
    if (!ui->dockWaveform->isVisible()) {
        return;
    }
    // =====================================================

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
        std::string pinName = waveformSignals[row].name;
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
        // Usar búsqueda binaria para encontrar el rango visible (O(log n) vs O(n))
        double searchStartTime = visibleStartTime - 1.0;  // -1s margen
        double searchEndTime = visibleEndTime + 1.0;      // +1s margen

        // Buscar índice de inicio con lower_bound
        auto startIt = std::lower_bound(samples.begin(), samples.end(), searchStartTime,
            [](const WaveformSample& sample, double time) {
                return sample.timestamp < time;
            });
        size_t startIdx = (startIt != samples.begin())
            ? std::distance(samples.begin(), startIt - 1)  // Incluir muestra anterior
            : 0;

        // Buscar índice de fin con upper_bound
        auto endIt = std::upper_bound(samples.begin(), samples.end(), searchEndTime,
            [](double time, const WaveformSample& sample) {
                return time < sample.timestamp;
            });
        size_t endIdx = std::min(
            static_cast<size_t>(std::distance(samples.begin(), endIt)) + 1,
            samples.size()
        );

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

    // Auto-scroll: Seguir el tiempo actual solo cuando está capturando Y auto-scroll habilitado
    if (isCapturing && isAutoScrollEnabled && maxTime > 0) {
        // Calcular posición X del tiempo más reciente
        int targetX = static_cast<int>(maxTime * PIXELS_PER_SECOND);

        // Centrar el viewport en el tiempo actual (con margen del 80% del ancho)
        int viewportWidth = ui->graphicsViewWaveform->viewport()->width();
        int scrollPos = targetX - static_cast<int>(viewportWidth * 0.8);

        // Asegurar que no hacemos scroll a valores negativos
        if (scrollPos < 0) scrollPos = 0;

        // Aplicar scroll a las tres vistas sincronizadas
        ui->graphicsViewWaveform->horizontalScrollBar()->setValue(scrollPos);
        timelineView->horizontalScrollBar()->setValue(scrollPos);
    }

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

void MainWindow::onPinsDataReady(std::shared_ptr<const std::vector<JTAG::PinLevel>> pins)
{
    // FASE 2: Dereferencia shared_ptr UNA VEZ para obtener referencia al vector
    // NO HACE COPIA - solo accede al objeto compartido
    const std::vector<JTAG::PinLevel>& pinsRef = *pins;

    // Este slot se ejecuta en GUI thread (thread-safe vía Qt signals)
    // Reemplaza el código que estaba en onPollTimer()

    qDebug() << "[MainWindow::onPinsDataReady] Called with" << pinsRef.size() << "pins (shared_ptr)"
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
                                .arg(updateCount).arg(pinsRef.size()), 100);
    }

    if (!scanController || !isCapturing) {
        qDebug() << "[MainWindow::onPinsDataReady] SKIPPED - not capturing";
        return;
    }

    // Sample decimation: Only apply in continuous SAMPLE mode
    // SAMPLE 1x (single shot) always updates regardless of decimation
    if (currentJTAGMode == JTAGMode::SAMPLE) {
        sampleCounter++;
        if (sampleCounter < currentSampleDecimation) {
            return;  // Skip this sample update
        }
        sampleCounter = 0;  // Reset for next cycle
    }

    // 1. Actualizar tabla de pines
    updatePinsTable();

    // 2. Actualizar Control Panel (reemplaza updateWatchTable)
    updateControlPanel(pinsRef);

    // 3. Capturar muestra para waveform (SOLO SI ES VISIBLE)
    // ===== OPTIMIZACIÓN: Pasar vector BSR completo para acceso directo =====
    // En lugar de que waveform llame a getPin() por cada señal,
    // le pasamos el vector completo y hace acceso[index] directo
    if (ui->dockWaveform->isVisible()) {
        captureWaveformSample(pinsRef);  // ← Pasar vector BSR completo
    }
    // ====================================================================
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

    // CRÍTICO: Actualizar currentJTAGMode ANTES de setScanMode
    // Esto asegura que updatePinsTable() vea el modo correcto
    currentJTAGMode = (modeId == 0) ? JTAGMode::SAMPLE :
                      (modeId == 1) ? JTAGMode::SAMPLE_SINGLE_SHOT :
                      (modeId == 2) ? JTAGMode::EXTEST :
                      (modeId == 3) ? JTAGMode::INTEST :
                      JTAGMode::BYPASS;

    scanController->setScanMode(targetMode);

    // LOG: Imprimir modo actual por consola
    qDebug() << "[MainWindow] JTAG Mode changed to:" << modeName;

    // Sincronizar isCapturing con el auto-inicio del worker
    // Si el modo requiere polling (todos menos BYPASS) y tenemos dispositivo inicializado,
    // el worker se auto-inició, así que marcar como capturing
    if (currentJTAGMode != JTAGMode::BYPASS && isDeviceInitialized) {
        if (!isCapturing) {
            isCapturing = true;
            ui->actionRun->setText("Stop");
            qDebug() << "[MainWindow] Worker auto-started, isCapturing set to true";
        }
    }

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
                // Solo añadir pines editables (OUTPUT y INOUT) - normalizar a lowercase
                std::string typeLower = type;
                std::transform(typeLower.begin(), typeLower.end(), typeLower.begin(), ::tolower);
                if (typeLower == "output" || typeLower == "inout" || typeLower == "output2" || typeLower == "inout2") {
                    std::string pinNumber = scanController->getPinNumber(pinName);
                    controlPanel->addPin(pinName, pinNumber);

                    // Obtener valor actual del pin desde la tabla de pines
                    QString qPinName = QString::fromStdString(pinName);
                    for (int row = 0; row < ui->tableWidgetPins->rowCount(); ++row) {
                        QTableWidgetItem* nameItem = ui->tableWidgetPins->item(row, 0);
                        if (nameItem && resolveRealPinName(nameItem->text()) == qPinName) {
                            // Encontrado - leer el valor de la columna 3 (I/O Value)
                            QTableWidgetItem* valueItem = ui->tableWidgetPins->item(row, 3);
                            if (valueItem) {
                                QString valueText = valueItem->text();
                                JTAG::PinLevel currentLevel;

                                // Convertir texto a PinLevel
                                if (valueText == "1") {
                                    currentLevel = JTAG::PinLevel::HIGH;
                                } else if (valueText == "0") {
                                    currentLevel = JTAG::PinLevel::LOW;
                                } else {
                                    currentLevel = JTAG::PinLevel::HIGH_Z;
                                }

                                // Actualizar el Control Panel con el valor actual
                                controlPanel->updatePinValue(pinName, currentLevel);
                            }
                            break;
                        }
                    }
                }
            }

            updateStatusBar(QString("Mode changed to %1 - Control Panel populated with current pin values").arg(modeName));
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
    if (!isEditingModeActive()) return;

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
    if (!isEditingModeActive()) return;

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
    if (!isEditingModeActive()) return;

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

// ============================================================================
// Mode Validation Helper
// ============================================================================

/**
 * @brief Checks if we are in a mode that allows pin editing
 *
 * Pin editing (setting pin values) is only allowed in EXTEST and INTEST modes.
 * In SAMPLE mode, pins are read-only.
 *
 * @return true if editing is allowed (EXTEST/INTEST), false otherwise
 */
bool MainWindow::isEditingModeActive()
{
    if (currentJTAGMode != JTAGMode::EXTEST && currentJTAGMode != JTAGMode::INTEST) {
        QMessageBox::warning(this, "Mode Error",
            "Pin editing is only available in EXTEST or INTEST mode.\n"
            "Current mode: SAMPLE (read-only)");
        return false;
    }
    return true;
}

// ============================================================================
// Window State Persistence
// ============================================================================

/**
 * @brief Saves the current window state to persistent storage
 *
 * Saves:
 * - Window geometry (size and position)
 * - Dock widgets state (visible, floating, position)
 * - Main window state (toolbars, splitters)
 * - Table column widths
 *
 * Uses QSettings with INI format
 * Settings are stored in "layout.ini" file in the working directory
 * (typically the same folder as the executable)
 */
void MainWindow::saveWindowState()
{
    QSettings settings("layout.ini", QSettings::IniFormat);

    // Save window geometry and state
    settings.setValue("MainWindow/geometry", saveGeometry());
    settings.setValue("MainWindow/windowState", saveState());

    // Save table column widths
    if (ui->tableWidgetPins->columnCount() > 0) {
        QList<int> columnWidths;
        for (int i = 0; i < ui->tableWidgetPins->columnCount(); ++i) {
            columnWidths.append(ui->tableWidgetPins->columnWidth(i));
        }
        settings.setValue("PinsTable/columnWidths", QVariant::fromValue(columnWidths));
    }

    // Save splitter states if any
    // Add more settings as needed

    settings.sync();
    qDebug() << "[MainWindow] Window state saved to:" << settings.fileName();
}

/**
 * @brief Loads the saved window state from persistent storage
 *
 * Restores:
 * - Window geometry (size and position)
 * - Dock widgets state (visible, floating, position)
 * - Main window state (toolbars, splitters)
 * - Table column widths
 *
 * Should be called after UI is fully initialized but before showing the window
 */
void MainWindow::loadWindowState()
{
    QSettings settings("layout.ini", QSettings::IniFormat);

    qDebug() << "[MainWindow] Loading window state from:" << settings.fileName();

    // Restore window geometry and state
    QByteArray geometry = settings.value("MainWindow/geometry").toByteArray();
    if (!geometry.isEmpty()) {
        restoreGeometry(geometry);
        qDebug() << "[MainWindow] Window geometry restored";
    } else {
        qDebug() << "[MainWindow] No saved geometry found, using defaults";
    }

    QByteArray windowState = settings.value("MainWindow/windowState").toByteArray();
    if (!windowState.isEmpty()) {
        restoreState(windowState);
        qDebug() << "[MainWindow] Window state (docks, toolbars) restored";
    }

    // Restore table column widths
    QList<int> columnWidths = settings.value("PinsTable/columnWidths").value<QList<int>>();
    if (!columnWidths.isEmpty() && ui->tableWidgetPins->columnCount() == columnWidths.size()) {
        for (int i = 0; i < columnWidths.size(); ++i) {
            ui->tableWidgetPins->setColumnWidth(i, columnWidths[i]);
        }
        qDebug() << "[MainWindow] Table column widths restored";
    }

    // Restore splitter states if any
    // Add more settings as needed
}
