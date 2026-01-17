/**
 * @file mainwindow.cpp
 * @brief Implementación de la ventana principal de JtagScannerQt_UVa
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
#include <QAbstractButton>
#include <QPushButton>
#include <QLabel>
#include <QDir>
#include <QStringList>
#include <QTimer>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QSplitter>
#include <QScrollBar>
#include <QKeyEvent>
#include <QPainterPath>
#include <QDialog>
#include <QListWidget>
#include <QDialogButtonBox>
#include <QMetaType>
#include <QSettings>
#include <QMenu>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QFile>
#include <QDateTime>
#include <filesystem>


// Standard Library
#include <iostream>
#include <iomanip>
#include <cmath>
#include <algorithm>

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
    , m_cursorSelector(nullptr)
    , m_activeCursor(ActiveCursor::NONE)
{
    // Cargar diseño UI desde mainwindow.ui
    ui->setupUi(this);

    typedef std::shared_ptr<const std::vector<JTAG::PinLevel>> MyPinPtr;

    qRegisterMetaType<MyPinPtr>("std::shared_ptr<const std::vector<JTAG::PinLevel>>");
    qRegisterMetaType<MyPinPtr>("std::shared_ptr<const std::vector<PinLevel>>");       // Sin JTAG::
    qRegisterMetaType<MyPinPtr>("std::shared_ptr<const std::vector<JTAG::PinLevel> >"); // Con espacio extra al final
    qRegisterMetaType<MyPinPtr>("std::shared_ptr<const std::vector<PinLevel> >");       // Sin JTAG:: y con espacio

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

    // ===== RENDER THROTTLING: Configurar timer con FPS configurables =====
    // Soluciona Event Loop Starvation con polling ultra-rápido (1ms = 1000 Hz)
    // Captura de datos: hasta 1000 Hz
    // Renderizado UI: limitado a FPS configurables (default: 30 FPS)
    m_waveformRenderTimer = new QTimer(this);
    int waveformIntervalMs = 1000 / currentWaveformFPS;
    m_waveformRenderTimer->setInterval(waveformIntervalMs);
    connect(m_waveformRenderTimer, &QTimer::timeout, this, [this]() {
        // Solo procesar si hay cambios pendientes y el dock es visible
        if (m_waveformNeedsRedraw && ui->dockWaveform->isVisible()) {
            // ===== PROCESAR BATCH DE MUESTRAS ACUMULADAS =====
            // Esto procesa TODAS las muestras pendientes de una sola vez
            for (const auto& sample : m_pendingSamples) {
                // Agregar cada muestra al buffer de cada señal
                for (const auto& sigInfo : waveformSignals) {
                    if (sigInfo.dataIndex >= 0 && sigInfo.dataIndex < static_cast<int>(sample.pins.size())) {
                        JTAG::PinLevel level = sample.pins[sigInfo.dataIndex];
                        auto& buffer = waveformBuffer[sigInfo.name];
                        buffer.push_back({sample.timestamp, level});
                        if (buffer.size() > MAX_WAVEFORM_SAMPLES) {
                            buffer.pop_front();
                        }
                    }
                }
            }
            m_pendingSamples.clear();  // Limpiar batch procesado
            // ================================================

            redrawWaveform();
            m_waveformNeedsRedraw = false;
        }
    });
    qDebug() << "[MainWindow] Waveform render timer configured at" << currentWaveformFPS << "FPS (" << waveformIntervalMs << "ms)";
    // Timer se inicia automáticamente cuando se añaden señales al waveform

    // ChipVisualizer render throttling timer
    m_chipVisRenderTimer = new QTimer(this);
    int chipVisIntervalMs = 1000 / currentChipVisFPS;
    m_chipVisRenderTimer->setInterval(chipVisIntervalMs);
    m_chipVisNeedsRedraw = false;
    connect(m_chipVisRenderTimer, &QTimer::timeout, this, [this]() {
        // Aplicar cambios pendientes si hay y el visualizador es visible
        if (m_chipVisNeedsRedraw && chipVisualizer && chipVisualizer->isVisible()) {
            for (const auto& [pinName, state] : m_pendingChipVisUpdates) {
                chipVisualizer->updatePinState(pinName, state);
            }
            m_pendingChipVisUpdates.clear();
            m_chipVisNeedsRedraw = false;
        }
    });
    m_chipVisRenderTimer->start();  // Siempre activo
    qDebug() << "[MainWindow] ChipVisualizer render timer configured at" << currentChipVisFPS << "FPS (" << chipVisIntervalMs << "ms)";

    // PinsTable render throttling timer (usa mismo FPS que ChipVisualizer)
    m_pinsTableRenderTimer = new QTimer(this);
    m_pinsTableRenderTimer->setInterval(chipVisIntervalMs);  // Mismo intervalo que ChipVis
    m_pinsTableNeedsRedraw = false;
    m_latestPinsData = nullptr;
    connect(m_pinsTableRenderTimer, &QTimer::timeout, this, [this]() {
        if (m_pinsTableNeedsRedraw && m_latestPinsData && ui->tableWidgetPins->isVisible()) {
            updatePinsTable();
            updateControlPanel(*m_latestPinsData);
            m_pinsTableNeedsRedraw = false;
        }
    });
    m_pinsTableRenderTimer->start();  // Siempre activo
    qDebug() << "[MainWindow] PinsTable render timer configured at" << currentChipVisFPS << "FPS (" << chipVisIntervalMs << "ms)";
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
    QSettings settings("UVa", "JtagScannerQt");
    currentPollInterval = settings.value("performance/pollInterval", 100).toInt();
    currentSampleDecimation = settings.value("performance/sampleDecimation", 1).toInt();
    currentSamplesPerSecond = settings.value("performance/samplesPerSecond", 10).toInt();
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

    // Deshabilitar acciones del menú Pins por defecto (solo activas en EXTEST/INTEST)
    ui->actionSet_to_0->setEnabled(false);
    ui->actionSet_to_1->setEnabled(false);
    ui->actionSet_to_Z->setEnabled(false);
    ui->actionSet_Bus_Value->setEnabled(false);
    ui->actionSet_Bus_to_All_Z->setEnabled(false);
    ui->actionSet_All_Device_Pins_to_BSDL_Safe->setEnabled(false);
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

    // ===== Cargar configuración guardada =====
    QSettings settings("UVa", "JtagScannerQt");

    // Cargar samples/second (default: 10 samples/s)
    int savedSamplesPerSecond = settings.value("performance/samplesPerSecond", 10).toInt();
    currentSamplesPerSecond = savedSamplesPerSecond;
    scanController->setSamplesPerSecond(savedSamplesPerSecond);

    // Cargar waveform FPS (default: 30 FPS)
    int savedWaveformFPS = settings.value("performance/waveformFPS", 30).toInt();
    currentWaveformFPS = savedWaveformFPS;
    // Note: m_waveformRenderTimer se configura más adelante en el constructor

    // Cargar chip visualizer FPS (default: 10 FPS)
    int savedChipVisFPS = settings.value("performance/chipVisFPS", 10).toInt();
    currentChipVisFPS = savedChipVisFPS;

    qDebug() << "[MainWindow] Loaded settings - Samples/s:" << savedSamplesPerSecond
             << ", Waveform FPS:" << savedWaveformFPS
             << ", ChipVis FPS:" << savedChipVisFPS;
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
    timelineView->setFixedHeight(30);
    timelineView->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    timelineView->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    timelineView->setAlignment(Qt::AlignTop | Qt::AlignLeft);  // Alinear arriba para que se vean las etiquetas
    timelineView->setStyleSheet("background-color: rgb(245, 245, 245); border-bottom: 1px solid rgb(200, 200, 200);");

    // Create fixed names view (left side, 150px wide)
    waveformNamesView = new QGraphicsView(this);
    waveformNamesView->setScene(waveformNamesScene);
    waveformNamesView->setRenderHint(QPainter::Antialiasing);
    waveformNamesView->setFixedWidth(150);
    waveformNamesView->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    waveformNamesView->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    waveformNamesView->setStyleSheet("background-color: rgb(245, 245, 245); border-right: 2px solid rgb(180, 180, 180);");

    // Configurar menú contextual para la vista de nombres de waveform
    waveformNamesView->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(waveformNamesView, &QGraphicsView::customContextMenuRequested,
            this, &MainWindow::onWaveformContextMenu);

    // Configure existing waveform view
    ui->graphicsViewWaveform->setScene(waveformScene);
    ui->graphicsViewWaveform->setRenderHint(QPainter::Antialiasing);

    // Install event filter para capturar teclas para navegación de cursores
    ui->graphicsViewWaveform->installEventFilter(this);
    ui->graphicsViewWaveform->setFocusPolicy(Qt::StrongFocus);

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
    timelineRow->setFixedHeight(30);
    timelineRow->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);

    // Create container widget to hold timeline row and waveform row
    QWidget* waveformContainer = new QWidget();
    waveformContainer->setObjectName("waveformContainer");
    QVBoxLayout* waveformLayout = new QVBoxLayout(waveformContainer);
    waveformLayout->setContentsMargins(0, 0, 0, 0);
    waveformLayout->setSpacing(0);

    // ============ CURSOR SELECTOR TOOLBAR ============
    QWidget* cursorToolbar = new QWidget();
    QHBoxLayout* cursorToolbarLayout = new QHBoxLayout(cursorToolbar);
    cursorToolbarLayout->setContentsMargins(5, 2, 5, 2);
    cursorToolbarLayout->setSpacing(5);

    // Create cursor selector combo box
    m_cursorSelector = new QComboBox(this);
    m_cursorSelector->addItem("No Cursor", static_cast<int>(ActiveCursor::NONE));
    m_cursorSelector->addItem("Cursor 1 (C1)", static_cast<int>(ActiveCursor::C1));
    m_cursorSelector->addItem("Cursor 2 (C2)", static_cast<int>(ActiveCursor::C2));
    m_cursorSelector->setToolTip("Select active cursor (use LEFT/RIGHT arrows to navigate transitions)");
    m_cursorSelector->setCurrentIndex(0);
    m_cursorSelector->setFixedWidth(150);

    m_cursorSelector->setFocusPolicy(Qt::NoFocus); 

    cursorToolbarLayout->addWidget(new QLabel("Cursor:"));
    cursorToolbarLayout->addWidget(m_cursorSelector);
    QFrame* line1 = new QFrame; line1->setFrameShape(QFrame::VLine); line1->setFrameShadow(QFrame::Sunken);
    cursorToolbarLayout->addWidget(line1);

    // 2. Etiqueta C1 (Naranja)
    m_lblC1Info = new QLabel("C1: --", this);
    m_lblC1Info->setStyleSheet("QLabel { font-weight: bold; color: #E67E00; min-width: 80px; }");
    cursorToolbarLayout->addWidget(m_lblC1Info);

    // Barra separadora
    QFrame* line2 = new QFrame; line2->setFrameShape(QFrame::VLine); line2->setFrameShadow(QFrame::Sunken);
    cursorToolbarLayout->addWidget(line2);

    // 3. Etiqueta C2 (Verde)
    m_lblC2Info = new QLabel("C2: --", this);
    m_lblC2Info->setStyleSheet("QLabel { font-weight: bold; color: #008000; min-width: 80px; }");
    cursorToolbarLayout->addWidget(m_lblC2Info);

    // Barra separadora
    QFrame* line3 = new QFrame; line3->setFrameShape(QFrame::VLine); line3->setFrameShadow(QFrame::Sunken);
    cursorToolbarLayout->addWidget(line3);

    // 4. Etiqueta Delta (Azul)
    m_lblDeltaInfo = new QLabel("ΔT: --", this);
    m_lblDeltaInfo->setStyleSheet("QLabel { font-weight: bold; color: #00008B; min-width: 100px; }");
    cursorToolbarLayout->addWidget(m_lblDeltaInfo);
    cursorToolbarLayout->addStretch();

    cursorToolbar->setFixedHeight(30);
    cursorToolbar->setStyleSheet("background-color: rgb(240, 240, 240); border-bottom: 1px solid rgb(200, 200, 200);");

    connect(m_cursorSelector, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &MainWindow::onCursorSelectorChanged);
    // =================================================

    // CRITICAL: Add with stretch factors: toolbar=0 (fixed), timeline=0 (fixed), waveform=1 (expanding)
    waveformLayout->addWidget(cursorToolbar, 0);    // No stretch - fixed height
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

    // Configurar menú contextual para la tabla de pines
    ui->tableWidgetPins->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(ui->tableWidgetPins, &QTableWidget::customContextMenuRequested,
            this, &MainWindow::onPinsTableContextMenu);

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

    // Deshabilitar botones por defecto (solo activos en EXTEST/INTEST)
    btnSetAllSafe->setEnabled(false);
    btnSetAll1->setEnabled(false);
    btnSetAllZ->setEnabled(false);
    btnSetAll0->setEnabled(false);

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
    radioIntest->setEnabled(false);  // Will be enabled after connection
    radioBypass->setEnabled(false);  // Will be enabled after connection
    btnSetAllSafe->setEnabled(false);
    btnSetAll1->setEnabled(false);
    btnSetAllZ->setEnabled(false);
    btnSetAll0->setEnabled(false);

    // Set tooltips for BYPASS and INTEST modes
    radioBypass->setToolTip("BYPASS mode - Transparent 1-bit DR, no BSR operations");
    radioIntest->setToolTip("INTEST mode - Test internal logic via boundary scan");
}

void MainWindow::setupConnections()
{
    // File menu connections
    connect(ui->actionNew_Project_Wizard, &QAction::triggered, this, &MainWindow::onNewProjectWizard);
    connect(ui->actionOpen_Project, &QAction::triggered, this, &MainWindow::onOpenProject);
    connect(ui->actionSave_Project, &QAction::triggered, this, &MainWindow::onSaveProject);
    connect(ui->actionExit, &QAction::triggered, this, &MainWindow::onExit);
    
    // View menu connections
    connect(ui->actionSettings, &QAction::triggered, this, &MainWindow::onSettings);

    // Scan menu connections
    connect(ui->actionJTAG_Connection, &QAction::triggered, this, &MainWindow::onJTAGConnection);
    connect(ui->actionExamine_Chain, &QAction::triggered, this, &MainWindow::onExamineChain);
    connect(ui->actionRun, &QAction::triggered, this, &MainWindow::onRun);
    connect(ui->actionReset, &QAction::triggered, this, &MainWindow::onReset);
    connect(ui->actionJTAG_Reset, &QAction::triggered, this, &MainWindow::onJTAGReset);
    connect(ui->actionDevice_BSDL_File, &QAction::triggered, this, &MainWindow::onDeviceBSDLFile);
    connect(ui->actionDevice_Package, &QAction::triggered, this, &MainWindow::onDevicePackage);
    connect(ui->actionDevice_Properties, &QAction::triggered, this, &MainWindow::onDeviceProperties);
    
    // Pins menu connections
    connect(ui->actionSearch_Pins, &QAction::triggered, this, &MainWindow::onSearchPins);
    connect(ui->actionEdit_Pin_Names_and_Buses, &QAction::triggered, this, &MainWindow::onEditPinNamesAndBuses);
    connect(ui->actionSet_to_0, &QAction::triggered, this, &MainWindow::onSetTo0);
    connect(ui->actionSet_to_1, &QAction::triggered, this, &MainWindow::onSetTo1);
    connect(ui->actionSet_to_Z, &QAction::triggered, this, &MainWindow::onSetToZ);
    //connect(ui->actionToggle, &QAction::triggered, this, &MainWindow::onTogglePinValue);
    connect(ui->actionSet_Bus_Value, &QAction::triggered, this, &MainWindow::onSetBusValue);
    connect(ui->actionSet_Bus_to_All_Z, &QAction::triggered, this, &MainWindow::onSetBusToAllZ);
    connect(ui->actionSet_All_Device_Pins_to_BSDL_Safe, &QAction::triggered, this, &MainWindow::onSetAllDevicePinsToBSDLSafe);
    
    // Watch menu connections
    connect(ui->actionWatch_Show, &QAction::triggered, this, &MainWindow::onWatchShow);

    // Waveform menu connections
    connect(ui->actionWaveform_Close, &QAction::triggered, this, &MainWindow::onWaveformClose);

    // Conectar señales de visibilidad de los docks para actualizar texto de acciones
    connect(ui->dockWatch, &QDockWidget::visibilityChanged, this, [this](bool visible) {
        ui->actionWatch_Show->setText(visible ? "Close Control Pins" : "Show Control Pins");
    });

    connect(ui->dockWaveform, &QDockWidget::visibilityChanged, this, [this](bool visible) {
        ui->actionWaveform_Close->setText(visible ? "Close Waveform" : "Show Waveform");
    });
    connect(ui->actionWaveform_Add_Signal, &QAction::triggered, this, &MainWindow::onWaveformAddSignal);
    connect(ui->actionWaveform_Remove, &QAction::triggered, this, &MainWindow::onWaveformRemove);
    connect(ui->actionWaveform_Remove_All, &QAction::triggered, this, &MainWindow::onWaveformRemoveAll);
    connect(ui->actionWaveform_Clear, &QAction::triggered, this, &MainWindow::onWaveformClear);
    connect(ui->actionWaveform_Zoom, &QAction::triggered, this, &MainWindow::onWaveformZoom);
    connect(ui->actionWaveform_Zoom_In, &QAction::triggered, this, &MainWindow::onWaveformZoomIn);
    connect(ui->actionWaveform_Zoom_Out, &QAction::triggered, this, &MainWindow::onWaveformZoomOut);
    connect(ui->actionWaveform_Go_to_Time, &QAction::triggered, this, &MainWindow::onWaveformGoToTime);
    connect(ui->actionWaveform_Set_Time_to_0, &QAction::triggered, this, &MainWindow::onWaveRestartTime);

    // Help menu connections
    connect(ui->actionHelp_Contents, &QAction::triggered, this, &MainWindow::onHelpContents);
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
    connect(ui->actionWaveRestartTime, &QAction::triggered, this, &MainWindow::onWaveRestartTime);
    connect(ui->actionWaveReload, &QAction::triggered, this, &MainWindow::onWaveReload);

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
    ui->actionDevice_BSDL_File->setEnabled(enable);
    ui->actionDevice_Properties->setEnabled(enable && isDeviceDetected);
    
    // Pin operations require initialized device
    ui->actionSet_to_0->setEnabled(enable && isDeviceInitialized);
    ui->actionSet_to_1->setEnabled(enable && isDeviceInitialized);
    ui->actionSet_to_Z->setEnabled(enable && isDeviceInitialized);
    //ui->actionToggle->setEnabled(enable && isDeviceInitialized);
    ui->actionSet_Bus_Value->setEnabled(enable && isDeviceInitialized);

    // Enable JTAG mode selector and quick action buttons
    radioSample->setEnabled(enable && isDeviceInitialized);
    radioSampleSingleShot->setEnabled(enable && isDeviceInitialized);
    radioExtest->setEnabled(enable && isDeviceInitialized);
    radioIntest->setEnabled(enable && isDeviceInitialized);
    radioBypass->setEnabled(enable && isDeviceInitialized);
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
    int result = wizard.exec();

    // Si el usuario eligió cargar un proyecto existente
    if (wizard.wantsToLoadProject()) {
        QTimer::singleShot(100, this, [this]() {
            onOpenProject();
        });
        return;
    }

    if (result == QDialog::Accepted) {

        // Resetear TODO el estado del proyecto anterior (sin preguntar)
        // Deja la aplicación como recién iniciada (excepto conexión del adaptador)
        resetProjectState();

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


void MainWindow::onExit()
{
    close();
}

// ============================================================================
// VIEW MENU SLOTS
// ============================================================================

/*void MainWindow::onTogglePins(bool checked)
{
    ui->dockPins->setVisible(checked);
}
*/
/*void MainWindow::onToggleWatch(bool checked)
{
    ui->dockWatch->setVisible(checked);
}*/

/*void MainWindow::onToggleWaveform(bool checked)
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
}*/

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
    dialog.setSamplesPerSecond(currentSamplesPerSecond);
    dialog.setWaveformFPS(currentWaveformFPS);
    dialog.setChipVisFPS(currentChipVisFPS);

    connect(&dialog, &SettingsDialog::pollingIntervalChanged,
            this, &MainWindow::onPollingIntervalChanged);
    connect(&dialog, &SettingsDialog::sampleDecimationChanged,
            this, &MainWindow::onSampleDecimationChanged);
    connect(&dialog, &SettingsDialog::samplesPerSecondChanged,
            this, &MainWindow::onSamplesPerSecondChanged);
    connect(&dialog, &SettingsDialog::waveformFPSChanged,
            this, &MainWindow::onWaveformFPSChanged);
    connect(&dialog, &SettingsDialog::chipVisFPSChanged,
            this, &MainWindow::onChipVisFPSChanged);

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
    QSettings settings("UVa", "JtagScannerQt");
    settings.setValue("performance/pollInterval", ms);

    updateStatusBar(QString("Polling interval: %1 ms").arg(ms));
}

void MainWindow::onSampleDecimationChanged(int decimation)
{
    currentSampleDecimation = decimation;
    sampleCounter = 0;  // Reset counter

    // Save to settings
    QSettings settings("UVa", "JtagScannerQt");
    settings.setValue("performance/sampleDecimation", decimation);

    QString msg = (decimation == 1)
        ? "Capturing all samples"
        : QString("Capturing 1 of every %1 samples").arg(decimation);
    updateStatusBar(msg);
}

void MainWindow::onSamplesPerSecondChanged(int samplesPerSec)
{
    currentSamplesPerSecond = samplesPerSec;

    // Apply to scan controller if available
    if (scanController) {
        scanController->setSamplesPerSecond(samplesPerSec);
    }

    // Save to settings
    QSettings settings("UVa", "JtagScannerQt");
    settings.setValue("performance/samplesPerSecond", samplesPerSec);

    updateStatusBar(QString("Sampling: %1 samples/s").arg(samplesPerSec));
}

void MainWindow::onWaveformFPSChanged(int fps)
{
    currentWaveformFPS = fps;

    // Update waveform render timer if available
    if (m_waveformRenderTimer) {
        int intervalMs = 1000 / fps;
        m_waveformRenderTimer->setInterval(intervalMs);
        qDebug() << "[MainWindow] Waveform FPS changed to" << fps << "(" << intervalMs << "ms interval)";
    }

    // Save to settings
    QSettings settings("UVa", "JtagScannerQt");
    settings.setValue("performance/waveformFPS", fps);

    updateStatusBar(QString("Waveform rendering: %1 FPS").arg(fps));
}

void MainWindow::onChipVisFPSChanged(int fps)
{
    currentChipVisFPS = fps;

    // Update chip visualizer render timer if available
    if (m_chipVisRenderTimer) {
        int intervalMs = 1000 / fps;
        m_chipVisRenderTimer->setInterval(intervalMs);
        qDebug() << "[MainWindow] ChipVisualizer FPS changed to" << fps << "(" << intervalMs << "ms interval)";
    }

    // Save to settings
    QSettings settings("UVa", "JtagScannerQt");
    settings.setValue("performance/chipVisFPS", fps);

    updateStatusBar(QString("Chip visualizer: %1 FPS").arg(fps));
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
        invalidatePinNameCache();

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

        updateStatusBar(QString("Device detected - IDCODE: 0x%1. Use File > New Project to configure.")
            .arg(idcode, 8, 16, QChar('0')));

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
        // PAUSE/RESUME: No reiniciamos desde 0, continuamos desde donde paramos
        // captureTimeOffset acumula el tiempo de sesiones anteriores

        // IMPORTANTE: NO cambiar el modo JTAG aquí
        // El modo ya fue configurado (SAMPLE, EXTEST, INTEST, etc.)
        // Solo necesitamos iniciar el polling con el modo actual

        // El worker ya está configurado con el modo correcto por onJTAGModeChanged()
        isCapturing = true;
        captureTimer.restart();  // Reinicia el timer local (offset preserva historia)
        scanController->startPolling();  // Iniciar worker thread (usa el modo actual)

        double totalTime = (captureTimeOffset / 1000.0);
        updateStatusBar(QString("Running - capturing in %1 mode (resuming from %2s)")
            .arg(currentJTAGMode == JTAGMode::SAMPLE ? "SAMPLE" :
                 currentJTAGMode == JTAGMode::EXTEST ? "EXTEST" :
                 currentJTAGMode == JTAGMode::INTEST ? "INTEST" : "current")
            .arg(totalTime, 0, 'f', 1));
        ui->actionRun->setText("Stop");
    } else {
        // PAUSE: Acumular tiempo transcurrido para reanudar después
        captureTimeOffset += captureTimer.elapsed();
        isCapturing = false;
        scanController->stopPolling();  // Detener worker thread

        double totalTime = (captureTimeOffset / 1000.0);
        updateStatusBar(QString("Paused at %1s").arg(totalTime, 0, 'f', 1));
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
        invalidatePinNameCache();

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

        // Limpiar waveform data
        waveformBuffer.clear();          // Borrar todas las muestras capturadas
        waveformSignals.clear();         // Borrar lista de señales agregadas
        captureTimeOffset = 0;           // Reset del offset de tiempo
        captureTimer.invalidate();       // Reset del timer de captura
        m_waveformNeedsRedraw = true;    // Marcar para redibujado

        // Limpiar buffers pendientes
        m_pendingChipVisUpdates.clear();
        m_chipVisNeedsRedraw = false;

        // Limpiar scenes
        if (waveformScene) {
            waveformScene->clear();
        }
        //EVITA excepciones al poner un cursor
        
        m_cursor1Line = nullptr;
        m_cursor2Line = nullptr;
        
        /*
        m_cursor1Label = nullptr;
        m_cursor2Label = nullptr;
        m_deltaLabel = nullptr;*/

        // También es vital resetear la lógica de los cursores
        m_cursor1Pos.defined = false;
        m_cursor2Pos.defined = false;
        m_activeCursor = ActiveCursor::NONE;
        if (waveformNamesScene) {
            waveformNamesScene->clear();
        }
        if (timelineScene) {
            timelineScene->clear();
        }

        // Limpiar contadores de transiciones (Watch panel)
        transitionCounters.clear();
        previousLevels.clear();

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

/**
 * @brief Selector de instrucción JTAG (no implementado)
 *
 * Placeholder para diálogo futuro de selección manual de instrucciones JTAG.
 * Actualmente las instrucciones se cambian mediante los botones SAMPLE/EXTEST.
 */
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

        // Resetear TODO el estado del proyecto anterior (sin preguntar)
        resetProjectState();

        // CARGAR NUEVO BSDL
#ifdef _WIN32
        std::filesystem::path bsdlPath(fileName.toStdWString());
#else
        std::filesystem::path bsdlPath(fileName.toStdString());
#endif

        if (scanController->loadBSDL(bsdlPath)) {
            updateStatusBar("BSDL loaded: " + fileName);
            currentBSDLPath = fileName;  // Guardar ruta del BSDL para el proyecto

            if (scanController->initializeDevice()) {
                isDeviceInitialized = true;
                isDeviceDetected = true;  // CORRECCIÓN: También marcar dispositivo como detectado

                // Reconectar todo
                updatePinsTable();
                renderChipVisualization();

                connect(scanController.get(), &JTAG::ScanController::pinsDataReady,
                    this, &MainWindow::onPinsDataReady);

                if (scanController->enterSAMPLE()) {
                    isCapturing = true;
                    captureTimer.start();
                    scanController->startPolling();
                    updateStatusBar("SAMPLE mode active");
                    ui->actionRun->setText("Stop");
                }
                enableControlsAfterConnection(true);

                // Forzar actualización de estado de controles según modo inicial (SAMPLE)
                // Esto asegura que los botones "All to" y acciones del menú Pins
                // estén desactivados correctamente al cargar el BSDL por primera vez
                onJTAGModeChanged(0);  // 0 = SAMPLE mode
            }
        }
        else {
            // Recuperar conexión en caso de fallo (opcional)
            connect(scanController.get(), &JTAG::ScanController::pinsDataReady,
                this, &MainWindow::onPinsDataReady);
            QMessageBox::critical(this, "Error", "Failed to load BSDL file");
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
    // Handle column 0 (Name) changes - Renaming
    if (item->column() == 0) {
        QString newDisplayName = item->text().trimmed();
        QString oldRealName = item->data(Qt::UserRole).toString();

        // Si el nombre no cambió realmente, no hacer nada
        if (oldRealName == newDisplayName || newDisplayName.isEmpty()) {
            return;
        }

        qDebug() << "[onPinTableItemChanged] Renaming pin:" << oldRealName << "->" << newDisplayName;

        // Verificar que tenemos un DeviceModel
        if (!scanController || !scanController->getDeviceModel()) {
            QMessageBox::warning(this, "Cannot Rename", "No device model loaded");
            item->setText(oldRealName); // Revertir cambio
            return;
        }

        // Intentar renombrar en el DeviceModel
        if (scanController->getDeviceModel()->renamePinAlias(
                oldRealName.toStdString(),
                newDisplayName.toStdString())) {

            // Actualizar UserRole para que coincida con el nuevo nombre
            item->setData(Qt::UserRole, newDisplayName);

            // Propagar a waveformSignals
            for (auto& sig : waveformSignals) {
                if (sig.name == oldRealName.toStdString()) {
                    sig.name = newDisplayName.toStdString();
                    qDebug() << "[onPinTableItemChanged] Updated waveform signal name";
                }
            }

            // Propagar a waveformBuffer (renombrar key del map)
            auto it = waveformBuffer.find(oldRealName.toStdString());
            if (it != waveformBuffer.end()) {
                auto data = std::move(it->second);
                waveformBuffer.erase(it);
                waveformBuffer[newDisplayName.toStdString()] = std::move(data);
                qDebug() << "[onPinTableItemChanged] Updated waveform buffer key";
            }

            // Propagar a control panel
            if (controlPanel) {
                controlPanel->renamePinIfExists(oldRealName, newDisplayName);
            }

            // Propagar a chip visualizer
            if (chipVisualizer) {
                chipVisualizer->updatePinName(oldRealName, newDisplayName);
            }

            // Invalidar cache de transiciones (los nombres en el cache pueden cambiar)
            m_transitionCache.dirty = true;

            // Redibujar waveform
            m_waveformNeedsRedraw = true;

            updateStatusBar(QString("Pin renamed: %1 → %2").arg(oldRealName).arg(newDisplayName));
            qDebug() << "[onPinTableItemChanged] Pin rename complete";
        } else {
            // Renombrado falló (posiblemente nombre duplicado)
            QMessageBox::warning(this, "Rename Failed",
                QString("Cannot rename pin to '%1'. Name may already exist or be invalid.")
                .arg(newDisplayName));
            item->setText(oldRealName); // Revertir cambio
        }

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
    // Renaming is done directly in the pins table by double-clicking
    // No dialog needed
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

/*void MainWindow::onTogglePinValue()
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
}*/

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
    // Toggle visibility del Control Panel
    bool isVisible = ui->dockWatch->isVisible();
    ui->dockWatch->setVisible(!isVisible);
    ui->actionWatch->setChecked(!isVisible);
}

// ============================================================================
// WAVEFORM MENU SLOTS
// ============================================================================

void MainWindow::onWaveformClose()
{
    // Toggle visibility del Waveform
    bool isVisible = ui->dockWaveform->isVisible();
    ui->dockWaveform->setVisible(!isVisible);
    ui->actionWaveform->setChecked(!isVisible);

    // Nota: El timer sigue corriendo aunque el waveform esté oculto
    // para que el tiempo y la captura de datos continúen
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
    // Confirmar si hay datos
    if (!waveformBuffer.empty()) {
        bool hasData = false;
        for (const auto& [name, samples] : waveformBuffer) {
            if (!samples.empty()) { hasData = true; break; }
        }
        if (hasData) {
            auto reply = QMessageBox::question(this, "Clear Waveform",
                "This will clear all waveform data and reset the timer to 0.\nContinue?",
                QMessageBox::Yes | QMessageBox::No);
            if (reply != QMessageBox::Yes) return;
        }
    }

    // 1. Limpiar buffers de datos
    for (auto& [name, samples] : waveformBuffer) {
        samples.clear();
    }
    m_pendingSamples.clear();

    // 2. Reiniciar el contador de tiempo
    captureTimeOffset = 0;
    captureTimer.invalidate();

    // 3. Limpiar la escena visual
    waveformScene->clear();
    m_cursor1Line = nullptr;
    m_cursor2Line = nullptr;

    // 4. Limpiar cursores
    m_cursor1Pos.defined = false;
    m_cursor2Pos.defined = false;
    m_transitionCache.dirty = true;

    // 5. Redibujar vacío
    redrawWaveform();

    updateStatusBar("Waveform cleared - timer reset to 0");
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
    QMessageBox msgBox(this);
    msgBox.setWindowTitle("Help - JtagScannerQt");
    msgBox.setTextFormat(Qt::RichText);
    msgBox.setText(
        "<h2>JtagScannerQt - JTAG Boundary Scan Tool</h2>"

        "<h3>Getting Started</h3>"
        "<ol>"
        "<li>Click <b>Scan</b> button in the toolbar</li>"
        "<li>Select your JTAG probe and connection speed</li>"
        "<li>The <b>New Project Wizard</b> will open automatically</li>"
        "<li>In the wizard you can:"
        "<ul>"
        "<li>Load a BSDL file and select chip package type</li>"
        "<li>Or load an existing project (.json)</li>"
        "</ul></li>"
        "<li>Select JTAG mode and click <b>Run (F5)</b> to start</li>"
        "</ol>"

        "<h3>JTAG Modes</h3>"
        "<ul>"
        "<li><b>SAMPLE</b> - Read pin states continuously (non-invasive observation)</li>"
        "<li><b>SAMPLE Single-Shot</b> - Read pin states once and stop</li>"
        "<li><b>EXTEST</b> - Control output pins externally (active mode)</li>"
        "<li><b>INTEST</b> - Test internal chip logic via boundary scan</li>"
        "<li><b>BYPASS</b> - Minimal scan chain for multi-device chains</li>"
        "</ul>"

        "<h3>Main Panels</h3>"
        "<ul>"
        "<li><b>Pins Table</b> - View and edit all device pins with filtering</li>"
        "<li><b>Control Panel</b> - Quick access to watched pins for manual control</li>"
        "<li><b>Waveform Viewer</b> - Digital signal viewer with cursors and zoom</li>"
        "<li><b>Chip Visualizer</b> - Visual package pin-out representation</li>"
        "</ul>"

        "<h3>Keyboard Shortcuts</h3>"
        "<table>"
        "<tr><td><b>F5</b></td><td>Run/Stop capture</td></tr>"
        "<tr><td><b>F6</b></td><td>Reset device</td></tr>"
        "<tr><td><b>Ctrl+S</b></td><td>Save project</td></tr>"
        "<tr><td><b>Ctrl+O</b></td><td>Open project</td></tr>"
        "</table>"

        "<h3>Tips</h3>"
        "<ul>"
        "<li>Right-click on pins to add them to Control Panel or Waveform</li>"
        "<li>Use <b>View > Settings</b> to adjust sampling rate and performance</li>"
        "<li>Waveform automatically decimates data when zoomed out</li>"
        "<li>Use the <b>T=0</b> button to reset waveform time without removing signals</li>"
        "<li>Run/Stop pauses capture - use Waveform > Clear to start fresh</li>"
        "</ul>"
    );
    msgBox.exec();
}

void MainWindow::onAbout()
{
    QMessageBox msgBox(this);
    msgBox.setWindowTitle("About JtagScannerQt");
    msgBox.setTextFormat(Qt::RichText);
    msgBox.setMinimumWidth(450);
    msgBox.setText(
        "<h2 style='text-align:center;'>JtagScannerQt</h2>"
        "<p style='text-align:center;'><b>JTAG Boundary Scan Tool</b></p>"
        "<p style='text-align:center;'>Version 1.0</p>"
        "<hr>"
        "<table width='100%'>"
        "<tr><td><b>Autor:</b></td><td>Álvaro Sacristán de Frutos</td></tr>"
        "<tr><td><b>Institución:</b></td><td>Universidad de Valladolid</td></tr>"
        "<tr><td><b>Departamento:</b></td><td>Departamento de Electrónica</td></tr>"
        "<tr><td><b>Centro:</b></td><td>E.T.S.I. de Telecomunicación</td></tr>"
        "</table>"
        "<hr>"
        "<p style='text-align:center;'><small>Built with Qt 6.7.3</small></p>"
    );
    msgBox.exec();
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

void MainWindow::onWaveRestartTime()
{
    // Limpiar datos de todas las señales (pero mantener las señales en la lista)
    for (auto& [name, samples] : waveformBuffer) {
        samples.clear();
    }
    m_pendingSamples.clear();

    // Reiniciar el contador de tiempo a 0
    captureTimeOffset = 0;
    captureTimer.restart();

    // Limpiar cursores
    m_cursor1Pos.defined = false;
    m_cursor2Pos.defined = false;
    m_transitionCache.dirty = true;

    // Redibujar (vacío pero con las señales esperando)
    redrawWaveform();

    updateStatusBar("Time reset to 0 - signals ready for capture");
}

void MainWindow::onWaveReload()
{
    // Solo redibujar sin borrar datos ni resetear tiempo
    redrawWaveform();
    updateStatusBar("Waveform reloaded");
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

    if (scanController->getDeviceModel() == nullptr) {
        ui->tableWidgetPins->setRowCount(0); // Asegurar tabla vacía
        invalidatePinNameCache();
        return;
    }

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
        invalidatePinNameCache();  // Invalidar cache al reconstruir tabla

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

        // 3. Leer estado del pin - OPTIMIZADO: usar m_latestPinsData si disponible
        std::optional<JTAG::PinLevel> level;

        if (m_latestPinsData && !m_latestPinsData->empty()) {
            // ===== ACCESO DIRECTO AL BUFFER - SIN LLAMADAS A FUNCIÓN =====
            // Determinar qué celda usar según el tipo de pin y modo
            int cellIndex = -1;
            if (currentJTAGMode == JTAGMode::EXTEST || currentJTAGMode == JTAGMode::INTEST) {
                // En EXTEST/INTEST: usar outputCell (lo que enviamos)
                cellIndex = pinInfo->outputCell;
                if (cellIndex < 0) cellIndex = pinInfo->inputCell;  // fallback
            } else {
                // En SAMPLE: usar inputCell (lo que recibimos)
                cellIndex = pinInfo->inputCell;
                if (cellIndex < 0) cellIndex = pinInfo->outputCell;  // fallback
            }

            if (cellIndex >= 0 && cellIndex < static_cast<int>(m_latestPinsData->size())) {
                level = (*m_latestPinsData)[cellIndex];
            }
            // =============================================================
        } else {
            // Fallback: llamar a getPin si no hay datos en buffer
            level = scanController->getPin(pinName);
        }

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

            // 5. Actualizar visualizador del chip (throttled render)
            // ===== OPTIMIZACIÓN: Acumular cambios y renderizar a FPS configurables =====
            if (chipVisualizer) {
                m_pendingChipVisUpdates[realName] = visualState;
                m_chipVisNeedsRedraw = true;
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

                    // Mantener estado LINKAGE (negro) en visualizador (throttled render)
                    if (chipVisualizer) {
                        m_pendingChipVisUpdates[realName] = VisualPinState::LINKAGE;
                        m_chipVisNeedsRedraw = true;
                    }
                } else {
                    // Pin normal sin valor (no accesible)
                    valueItem->setText("?");
                    valueItem->setFlags(valueItem->flags() & ~Qt::ItemIsEditable);
                    valueItem->setBackground(Qt::lightGray);

                    // Actualizar visualizador como UNKNOWN (gris, throttled render)
                    if (chipVisualizer) {
                        m_pendingChipVisUpdates[realName] = VisualPinState::UNKNOWN;
                        m_chipVisNeedsRedraw = true;
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

void MainWindow::rebuildPinNameCache() const
{
    m_displayToRealNameCache.clear();
    m_displayToRealNameCache.reserve(ui->tableWidgetPins->rowCount());

    for (int row = 0; row < ui->tableWidgetPins->rowCount(); row++) {
        QTableWidgetItem* nameItem = ui->tableWidgetPins->item(row, 0);
        if (nameItem) {
            QString displayName = nameItem->text();
            QString realName = nameItem->data(Qt::UserRole).toString();
            m_displayToRealNameCache[displayName] = realName.isEmpty() ? displayName : realName;
        }
    }
    m_pinNameCacheValid = true;
}

QString MainWindow::resolveRealPinName(const QString& displayName) const
{
    // ===== OPTIMIZACIÓN: Usar cache O(1) en lugar de búsqueda O(n) =====
    // Antes: 200 pines × 200 filas = 40,000 iteraciones/update
    // Ahora: 200 pines × O(1) lookup = 200 operaciones/update

    if (!m_pinNameCacheValid) {
        rebuildPinNameCache();
    }

    return m_displayToRealNameCache.value(displayName, displayName);
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
    // Validación de seguridad
    if (!scanController || !controlPanel || !scanController->getDeviceModel()) return;

    // Solo actualizamos si estamos en EXTEST o INTEST
    if (currentJTAGMode != JTAGMode::EXTEST && currentJTAGMode != JTAGMode::INTEST) return;

    // Bloqueamos señales para evitar bucles de feedback
    const bool wasBlocked = controlPanel->signalsBlocked();
    controlPanel->blockSignals(true);

    const auto* deviceModel = scanController->getDeviceModel();

    // ===== OPTIMIZACIÓN: Solo iterar sobre pines en el Control Panel =====
    // Antes: 200+ pines del dispositivo × getPinInfo() = 200+ lookups
    // Ahora: Solo 5-10 pines observados = 5-10 lookups
    // Accedemos directamente al QTableWidget interno del ControlPanel
    QTableWidget* cpTable = controlPanel->findChild<QTableWidget*>();
    if (!cpTable) {
        controlPanel->blockSignals(wasBlocked);
        return;
    }

    for (int row = 0; row < cpTable->rowCount(); row++) {
        QTableWidgetItem* nameItem = cpTable->item(row, 0);
        if (!nameItem) continue;

        std::string pinName = nameItem->text().toStdString();
        auto pinInfo = deviceModel->getPinInfo(pinName);

        // Si el pin tiene celda de salida
        if (pinInfo && pinInfo->outputCell >= 0) {
            size_t index = static_cast<size_t>(pinInfo->outputCell);

            // Si el índice es válido
            if (index < pinLevels.size()) {
                JTAG::PinLevel level = pinLevels[index];
                controlPanel->updatePinValue(pinName, level);
            }
        }
    }
    // =====================================================================

    // Restauramos señales
    controlPanel->blockSignals(wasBlocked);
}
void MainWindow::captureWaveformSample(const std::vector<JTAG::PinLevel>& currentPins)
{
    // ==================== PUNTO DE INTEGRACIÓN 13 ====================
    if (waveformSignals.empty()) return;

    // Tiempo total = offset acumulado + tiempo actual de esta sesión
    double currentTime = (captureTimeOffset + captureTimer.elapsed()) / 1000.0;

    // ===== OPTIMIZACIÓN MÁXIMA: Acceso directo por índice =====
    // Elimina TODAS las búsquedas, hash lookups y llamadas de función
    // Antes: 20 señales × 1000Hz × getPin() = 20,000 llamadas/seg
    // Ahora: 20 señales × 1000Hz × acceso[i] = acceso directo a memoria
    for (const auto& sigInfo : waveformSignals) {
        // Validación de seguridad (bounds checking)
        if (sigInfo.dataIndex >= 0 && sigInfo.dataIndex < static_cast<int>(currentPins.size())) {
            // *** ACCESO DIRECTO A MEMORIA - INSTANTÁNEO ***
            JTAG::PinLevel level = currentPins[sigInfo.dataIndex];

            // ===== OPTIMIZACIÓN: Cachear referencia al buffer (1 lookup en lugar de 3) =====
            auto& buffer = waveformBuffer[sigInfo.name];
            buffer.push_back({currentTime, level});

            // Maintain circular buffer
            if (buffer.size() > MAX_WAVEFORM_SAMPLES) {
                buffer.pop_front();
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

    // Invalidar cache de transiciones (hay nuevos datos)
    m_transitionCache.dirty = true;
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

        m_cursor1Line = nullptr;
        m_cursor2Line = nullptr;
/*
        m_cursor1Label = nullptr;
        m_cursor2Label = nullptr;
        m_deltaLabel = nullptr;
        */
        waveformNamesScene->clear();
        timelineScene->clear();
        isRedrawing = false;
        return;  // NO dibujar grid ni timeline cuando no hay señales
    }

    waveformScene->clear();

    m_cursor1Line = nullptr;
    m_cursor2Line = nullptr;

/*
    m_cursor1Label = nullptr;
    m_cursor2Label = nullptr;
    m_deltaLabel = nullptr;
    */

    waveformNamesScene->clear();
    timelineScene->clear();

    // FIXED SIGNAL_HEIGHT - Las señales NO se estiran al agrandar ventana
    const int SIGNAL_HEIGHT = 30;      // Vertical space per signal (FIXED) - Reducido para más densidad
    const int HIGH_Y_OFFSET = 7;       // Y offset for HIGH level
    const int LOW_Y_OFFSET = 23;       // Y offset for LOW level
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
    // PERO: Si auto-scroll está activo, calcular donde VAMOS a scrollear (no donde estamos)
    QScrollBar* hScrollBar = ui->graphicsViewWaveform->horizontalScrollBar();
    int viewportWidthPixels = ui->graphicsViewWaveform->viewport()->width();
    if (viewportWidthPixels <= 0) viewportWidthPixels = 800;  // Seguridad

    int scrollPos;
    if (isCapturing && isAutoScrollEnabled && maxTime > 0) {
        // Predecir posición de auto-scroll para dibujar labels en la zona correcta
        int targetX = static_cast<int>(maxTime * PIXELS_PER_SECOND);
        scrollPos = targetX - static_cast<int>(viewportWidthPixels * 0.8);
        if (scrollPos < 0) scrollPos = 0;
    } else {
        // Usar posición actual del scroll
        scrollPos = (hScrollBar && !isEmpty) ? hScrollBar->value() : 0;
    }

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

    // ===== OPTIMIZACIÓN: Usar QPainterPath para agrupar líneas =====
    // En lugar de crear cientos de QGraphicsLineItem individuales,
    // agrupamos todas las líneas del mismo tipo en un solo QPainterPath.
    // Esto reduce drásticamente el número de objetos gráficos.

    // Grid MENOR - Batch en un solo path
    QPainterPath minorGridPath;
    QPainterPath minorTimelinePath;
    for (double t = gridStart; t <= gridEnd; t += gridMinorInterval) {
        if (t < 0) continue;
        int x = static_cast<int>(t * PIXELS_PER_SECOND);
        minorGridPath.moveTo(x, 0);
        minorGridPath.lineTo(x, maxY);
        minorTimelinePath.moveTo(x, 15);  // Ajustado para caber en 30px
        minorTimelinePath.lineTo(x, 28);
    }
    waveformScene->addPath(minorGridPath, gridMinorPen);
    timelineScene->addPath(minorTimelinePath, gridMinorPen);

    // Grid MAYOR - Batch en un solo path
    QPainterPath majorGridPath;
    QPainterPath majorTimelinePath;
    for (double t = gridStart; t <= gridEnd; t += gridMajorInterval) {
        if (t < 0) continue;
        int x = static_cast<int>(t * PIXELS_PER_SECOND);
        majorGridPath.moveTo(x, 0);
        majorGridPath.lineTo(x, maxY);
        majorTimelinePath.moveTo(x, 15);  // Ajustado para caber en 30px
        majorTimelinePath.lineTo(x, 28);
    }
    waveformScene->addPath(majorGridPath, gridMajorPen);
    timelineScene->addPath(majorTimelinePath, gridMajorPen);

    // W8: Dibujar etiquetas de tiempo SOLO en rango visible
    QPen timelinePen(QColor(100, 100, 100));

    // Dibujar etiquetas SOLO en marcas mayores visibles (optimización)
    for (double t = gridStart; t <= gridEnd; t += gridMajorInterval) {
        if (t < 0) continue;

        int x = static_cast<int>(t * PIXELS_PER_SECOND);

        // Línea vertical de tick más oscura (ajustado para 30px)
        timelineScene->addLine(x, 15, x, 28, timelinePen);

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
        // FIX: Asegurar que la etiqueta no quede en coordenadas negativas
        int labelX = std::max(2, x - 25);  // Mínimo 2px desde el borde izquierdo
        timeText->setPos(labelX, 2);
        timeText->setDefaultTextColor(QColor(40, 40, 40));
        timeText->setFont(QFont("Arial", 9, QFont::Bold));
    }

    // Línea horizontal base de la timeline (ajustado para 30px, solo rango visible)
    timelineScene->addLine(visibleStartX, 28, visibleEndX + 50, 28, QPen(QColor(150, 150, 150), 2));

    // Draw each signal
    for (int row = 0; row < waveformSignals.size(); row++) {
        std::string pinName = waveformSignals[row].name;
        auto& samples = waveformBuffer[pinName];

        int yBase = row * SIGNAL_HEIGHT;
        int yHigh = yBase + HIGH_Y_OFFSET;
        int yLow = yBase + LOW_Y_OFFSET;

        // Dibujar nombre en escena separada (waveformNamesScene)
        QGraphicsTextItem *label = waveformNamesScene->addText(QString::fromStdString(pinName));
        label->setPos(10, yBase + 7);
        label->setDefaultTextColor(Qt::black);
        label->setFont(QFont("Arial", 9, QFont::Bold));

        // Dibujar separador horizontal en escena de nombres
        waveformNamesScene->addLine(0, yBase + SIGNAL_HEIGHT, 150, yBase + SIGNAL_HEIGHT,
                                   QPen(QColor(180, 180, 180)));

        if (samples.empty()) continue;

        // Dibujar líneas de referencia para HIGH y LOW (muy tenues)
        // OPTIMIZACIÓN: Solo dibujar en el rango visible (no de 0 a maxX)
        QPen referencePen(QColor(230, 230, 230), 1, Qt::DashLine);
        waveformScene->addLine(visibleStartX, yHigh, visibleEndX + 50, yHigh, referencePen);  // HIGH level
        waveformScene->addLine(visibleStartX, yLow, visibleEndX + 50, yLow, referencePen);    // LOW level

        // Draw waveform
        QPen signalPen(Qt::blue, 2);
        QPen zPen(Qt::gray, 2, Qt::DashLine);

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

        // Calcular decimación basada en resolución de pantalla
        size_t visibleCount = endIdx - startIdx;
        size_t step = 1;

        // Obtener ancho del viewport en píxeles
        int viewportWidthPixels = ui->graphicsViewWaveform->viewport()->width();
        if (viewportWidthPixels <= 0) viewportWidthPixels = 800;

        // ===== OPTIMIZACIÓN: Diezmado más agresivo (1 muestra cada 2 píxeles) =====
        // Esto reduce a la mitad los segmentos dibujados sin pérdida visual significativa
        const size_t PIXELS_PER_SAMPLE = 2;  // Aumentar para más agresividad
        size_t maxSamples = viewportWidthPixels / PIXELS_PER_SAMPLE;
        if (maxSamples < 50) maxSamples = 50;  // Mínimo 50 muestras visibles

        if (visibleCount > maxSamples) {
            step = visibleCount / maxSamples;
            if (step < 1) step = 1;
        }

        // ===== OPTIMIZACIÓN: Agrupar todas las líneas de la señal en QPainterPath =====
        QPainterPath signalPath;      // Para líneas normales (HIGH/LOW)
        QPainterPath highZPath;       // Para líneas HIGH_Z (estilo diferente)

        // ===== MIN-MAX DECIMATION para preservar transiciones =====
        // En lugar de saltar muestras ciegamente, buscamos transiciones dentro del intervalo
        // Esto garantiza que no perdemos pulsos cortos al hacer zoom out

        size_t i = startIdx;
        JTAG::PinLevel lastDrawnLevel = (i < samples.size()) ? samples[i].level : JTAG::PinLevel::LOW;
        double lastDrawnX = (i < samples.size()) ? samples[i].timestamp * PIXELS_PER_SECOND : 0;
        int lastDrawnY = getLevelY(lastDrawnLevel, yBase);

        while (i < endIdx) {
            // Definir el intervalo de decimación [i, i+step)
            size_t intervalEnd = std::min(i + step, endIdx);

            // Buscar si hay alguna transición en el intervalo
            bool hasTransition = false;
            size_t transitionIdx = i;
            JTAG::PinLevel currentLevel = samples[i].level;

            for (size_t j = i; j < intervalEnd; ++j) {
                if (samples[j].level != currentLevel) {
                    hasTransition = true;
                    transitionIdx = j;
                    break;
                }
            }

            // Determinar qué muestra representará este intervalo
            size_t representativeIdx;
            if (hasTransition) {
                // Si hay transición, usamos el punto de transición (preserva flancos)
                representativeIdx = transitionIdx;
            } else {
                // Sin transición, usamos el último punto del intervalo
                representativeIdx = intervalEnd - 1;
            }

            const auto& sample = samples[representativeIdx];
            double x2 = sample.timestamp * PIXELS_PER_SECOND;
            int y2 = getLevelY(sample.level, yBase);

            // Dibujar línea horizontal desde último punto hasta aquí
            if (lastDrawnLevel == JTAG::PinLevel::HIGH_Z) {
                highZPath.moveTo(lastDrawnX, lastDrawnY);
                highZPath.lineTo(x2, lastDrawnY);
            } else {
                signalPath.moveTo(lastDrawnX, lastDrawnY);
                signalPath.lineTo(x2, lastDrawnY);
            }

            // Dibujar transición vertical si cambió el nivel
            if (lastDrawnY != y2) {
                signalPath.moveTo(x2, lastDrawnY);
                signalPath.lineTo(x2, y2);
            }

            // Actualizar estado
            lastDrawnX = x2;
            lastDrawnY = y2;
            lastDrawnLevel = sample.level;

            // Avanzar al siguiente intervalo
            i = intervalEnd;
        }

        // Añadir los paths como objetos únicos (mucho más eficiente que cientos de líneas)
        if (!signalPath.isEmpty()) {
            waveformScene->addPath(signalPath, signalPen);
        }
        if (!highZPath.isEmpty()) {
            waveformScene->addPath(highZPath, zPen);
        }

        // Draw separator line (OPTIMIZADO: solo rango visible)
        waveformScene->addLine(visibleStartX, yBase + SIGNAL_HEIGHT, visibleEndX + 50, yBase + SIGNAL_HEIGHT,
                              QPen(QColor(180, 180, 180)));
    }

    // Configurar tamaños de las escenas
    waveformScene->setSceneRect(0, 0, maxX, maxY);
    waveformNamesScene->setSceneRect(0, 0, 150, maxY);
    timelineScene->setSceneRect(0, 0, maxX, 30);  // Ajustado a 30px para coincidir con la vista

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

    // Renderizar cursores del waveform
    renderCursors();

    isRedrawing = false;
}

// ============================================================================
// HELPER METHODS
// ============================================================================

void MainWindow::updateWindowTitle(const QString &filename)
{
    if (filename.isEmpty()) {
        setWindowTitle("JtagScannerQt");
    } else {
        QFileInfo fileInfo(filename);
        setWindowTitle(fileInfo.fileName() + " - JtagScannerQt");
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
    // =================================================================
    // GUARDIAS DE SEGURIDAD (Critical Fix for Reload Crash)
    // =================================================================

    // 1. Si el controlador murió o se está reiniciando
    if (!scanController) return;

    // 2. Si el modelo no existe (se borró en unloadBSDL)
    if (!scanController->getDeviceModel()) return;

    // 3. Si el dispositivo no está marcado como inicializado y detectado
    if (!isDeviceInitialized || !isDeviceDetected) return;

    // 4. Si los datos recibidos no coinciden con el tamaño esperado del BSR actual
    // (Esto filtra los paquetes "zombis" del proyecto anterior que tenían otro tamaño)
    if (pins->size() != scanController->getDeviceModel()->getBSRLength()) {
        return;
    }
    // =================================================================

    // Si el modelo se está borrando o es null, salir inmediatamente.
    // Esto es lo que causa tu excepción "dangling reference".
    if (scanController->getDeviceModel() == nullptr) {
        return;
    }

    // FASE 2: Dereferencia shared_ptr UNA VEZ
    const std::vector<JTAG::PinLevel>& pinsRef = *pins;

    // --- El resto de tu función sigue exactamente igual ---

    // Debug info (opcional, puedes mantener tu código original aquí)
    // ...

    // Check if no target is detected logic...
    // ...

    if (!isCapturing) {
        return;
    }

    // Sample decimation logic...
    if (currentJTAGMode == JTAGMode::SAMPLE) {
        sampleCounter++;
        if (sampleCounter < currentSampleDecimation) {
            return;
        }
        sampleCounter = 0;
    }

    // ===== THROTTLED UPDATES =====
    // Guardar datos y marcar para actualización (el timer se encarga del render)
    m_latestPinsData = pins;  // shared_ptr, no copia
    m_pinsTableNeedsRedraw = true;

    // ===== BATCHING: Acumular muestras para waveform (procesadas en timer) =====
    // En lugar de llamar captureWaveformSample() 100 veces/s, acumulamos y procesamos en batch
    if (ui->dockWaveform->isVisible() && !waveformSignals.empty()) {
        // Tiempo total = offset acumulado + tiempo actual de esta sesión
        double currentTime = (captureTimeOffset + captureTimer.elapsed()) / 1000.0;
        m_pendingSamples.push_back({currentTime, pinsRef});  // Copia rápida al buffer
        m_waveformNeedsRedraw = true;
    }
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

    // Habilitar/deshabilitar botones de toolbar "All to" según modo de edición
    if (btnSetAllSafe) btnSetAllSafe->setEnabled(enableControlPanel);
    if (btnSetAll1) btnSetAll1->setEnabled(enableControlPanel);
    if (btnSetAllZ) btnSetAllZ->setEnabled(enableControlPanel);
    if (btnSetAll0) btnSetAll0->setEnabled(enableControlPanel);

    // Habilitar/deshabilitar acciones del menú Pins según modo de edición
    ui->actionSet_to_0->setEnabled(enableControlPanel);
    ui->actionSet_to_1->setEnabled(enableControlPanel);
    ui->actionSet_to_Z->setEnabled(enableControlPanel);
    ui->actionSet_Bus_Value->setEnabled(enableControlPanel);
    ui->actionSet_Bus_to_All_Z->setEnabled(enableControlPanel);
    ui->actionSet_All_Device_Pins_to_BSDL_Safe->setEnabled(enableControlPanel);

    updatePinsTable(); // Refrescar para habilitar/deshabilitar edición
}

// ----------------------------------------------------------------------------
// SET ALL TO SAFE (High-Z por defecto)
// ----------------------------------------------------------------------------
void MainWindow::onSetAllToSafeState()
{
    if (!scanController) return;
    // Use the menu action implementation which already exists
    onSetAllDevicePinsToBSDLSafe();
}

// ----------------------------------------------------------------------------
// SET ALL TO 1 (HIGH)
// ----------------------------------------------------------------------------
void MainWindow::onSetAllTo1()
{
    if (!scanController || !scanController->getDeviceModel()) return;
    if (!isEditingModeActive()) return;

    // 1. Obtenemos acceso directo a todos los pines (más rápido que strings)
    const auto& allPins = scanController->getDeviceModel()->getAllPins();
    int count = 0;

    for (const auto& pin : allPins) {
        // 2. FILTRO ROBUSTO: Si tiene celda de salida (outputCell >= 0), es escribible.
        // Esto cubre OUTPUT, INOUT, OUTPUT2, etc. automáticamente.
        if (pin.outputCell >= 0) {
            // 3. ASYNC: Usamos setPinAsync para no chocar con el worker
            scanController->setPinAsync(pin.name, JTAG::PinLevel::HIGH);
            count++;
        }
    }

    // 4. NO LLAMAR A applyChanges(). 
    // El worker detectará los pines sucios (dirty) y los aplicará automáticamente.

    updateStatusBar(QString("Queued %1 output pins to HIGH").arg(count));

    // La tabla se actualizará sola cuando el worker confirme los cambios (onPinsDataReady)
}

// ----------------------------------------------------------------------------
// SET ALL TO Z (HIGH-Z / TRISTATE)
// ----------------------------------------------------------------------------
void MainWindow::onSetAllToZ()
{
    if (!scanController || !scanController->getDeviceModel()) return;
    if (!isEditingModeActive()) return;

    const auto& allPins = scanController->getDeviceModel()->getAllPins();
    int count = 0;

    for (const auto& pin : allPins) {
        if (pin.outputCell >= 0) {
            scanController->setPinAsync(pin.name, JTAG::PinLevel::HIGH_Z);
            count++;
        }
    }

    updateStatusBar(QString("Queued %1 output pins to High-Z").arg(count));
}

// ----------------------------------------------------------------------------
// SET ALL TO 0 (LOW)
// ----------------------------------------------------------------------------
void MainWindow::onSetAllTo0()
{
    if (!scanController || !scanController->getDeviceModel()) return;
    if (!isEditingModeActive()) return;

    const auto& allPins = scanController->getDeviceModel()->getAllPins();
    int count = 0;

    for (const auto& pin : allPins) {
        if (pin.outputCell >= 0) {
            scanController->setPinAsync(pin.name, JTAG::PinLevel::LOW);
            count++;
        }
    }

    updateStatusBar(QString("Queued %1 output pins to LOW").arg(count));
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

// ============================================================================
// WAVEFORM CURSORS IMPLEMENTATION
// ============================================================================

void MainWindow::onCursorSelectorChanged(int index)
{
    m_activeCursor = static_cast<ActiveCursor>(m_cursorSelector->currentData().toInt());

    // Inicializar posición del cursor en viewport visible si no está definido
    if (m_activeCursor == ActiveCursor::C1 && !m_cursor1Pos.defined) {
        QScrollBar* hScrollBar = ui->graphicsViewWaveform->horizontalScrollBar();
        int scrollPos = hScrollBar ? hScrollBar->value() : 0;
        int viewportWidth = ui->graphicsViewWaveform->viewport()->width();
        double PIXELS_PER_SECOND = 100.0 / waveformTimebase;

        m_cursor1Pos.timePosition = (scrollPos + viewportWidth * 0.25) / PIXELS_PER_SECOND;
        m_cursor1Pos.defined = true;
    } else if (m_activeCursor == ActiveCursor::C2 && !m_cursor2Pos.defined) {
        QScrollBar* hScrollBar = ui->graphicsViewWaveform->horizontalScrollBar();
        int scrollPos = hScrollBar ? hScrollBar->value() : 0;
        int viewportWidth = ui->graphicsViewWaveform->viewport()->width();
        double PIXELS_PER_SECOND = 100.0 / waveformTimebase;

        m_cursor2Pos.timePosition = (scrollPos + viewportWidth * 0.75) / PIXELS_PER_SECOND;
        m_cursor2Pos.defined = true;
    }

    m_waveformNeedsRedraw = true;

    QString statusMsg = "No cursor";
    if (m_activeCursor == ActiveCursor::C1) statusMsg = "Cursor 1 active";
    else if (m_activeCursor == ActiveCursor::C2) statusMsg = "Cursor 2 active";
    updateStatusBar(statusMsg);
}

void MainWindow::updateTransitionCache()
{
    if (!m_transitionCache.dirty) return;

    m_transitionCache.timestamps.clear();

    // Recopilar todas las transiciones de todas las señales
    for (const auto& [name, samples] : waveformBuffer) {
        if (samples.size() < 2) continue;

        JTAG::PinLevel prevLevel = samples[0].level;
        for (size_t i = 1; i < samples.size(); ++i) {
            if (samples[i].level != prevLevel) {
                m_transitionCache.timestamps.push_back(samples[i].timestamp);
                prevLevel = samples[i].level;
            }
        }
    }

    // Ordenar y eliminar duplicados
    std::sort(m_transitionCache.timestamps.begin(),
              m_transitionCache.timestamps.end());
    auto last = std::unique(m_transitionCache.timestamps.begin(),
                           m_transitionCache.timestamps.end());
    m_transitionCache.timestamps.erase(last, m_transitionCache.timestamps.end());

    m_transitionCache.dirty = false;
}

double MainWindow::findNextTransition(double currentTime, bool forward)
{
    updateTransitionCache();

    if (m_transitionCache.timestamps.empty()) return currentTime;

    if (forward) {
        // Búsqueda binaria de primera transición > currentTime
        auto it = std::upper_bound(m_transitionCache.timestamps.begin(),
                                   m_transitionCache.timestamps.end(),
                                   currentTime);
        if (it != m_transitionCache.timestamps.end()) {
            return *it;
        }
        return currentTime;  // No hay más transiciones
    } else {
        // Búsqueda binaria de última transición < currentTime
        auto it = std::lower_bound(m_transitionCache.timestamps.begin(),
                                   m_transitionCache.timestamps.end(),
                                   currentTime);
        if (it != m_transitionCache.timestamps.begin()) {
            --it;
            return *it;
        }
        return currentTime;  // No hay transiciones previas
    }
}

void MainWindow::moveCursorByTransition(bool forward)
{
    // 1. Identificar qué cursor movemos
    CursorPosition* cursor = nullptr;
    if (m_activeCursor == ActiveCursor::C1) cursor = &m_cursor1Pos;
    else if (m_activeCursor == ActiveCursor::C2) cursor = &m_cursor2Pos;
    else return; // Ninguno seleccionado

    if (!cursor->defined) return;

    // 2. Buscar el siguiente flanco (transición)
    double newTime = findNextTransition(cursor->timePosition, forward);

    // Si ha habido cambio de posición
    if (newTime != cursor->timePosition) {

        // Actualizar datos
        cursor->timePosition = newTime;
        m_waveformNeedsRedraw = true; // Para redibujar las líneas verticales

        double x = newTime * (100.0 / waveformTimebase); // Conversión Tiempo -> Píxeles
        ui->graphicsViewWaveform->centerOn(x, 0);        // Centrar cámara

        
        // Calculamos dónde cae ese tiempo en píxeles (X)
        // Fórmula: X = Tiempo * (100 píxeles / unidad de base de tiempo)
        const double PIXELS_PER_SECOND = 100.0 / waveformTimebase;
        double targetX = newTime * PIXELS_PER_SECOND;

        // centerOn toma una coordenada de la ESCENA y mueve el scroll
        // para que ese punto quede en el CENTRO del visor.
        ui->graphicsViewWaveform->centerOn(targetX, 0);

        // Debug opcional en barra de estado
        // updateStatusBar(QString("Jumped to %1 ms").arg(newTime * 1000.0));
    }
}

void MainWindow::renderCursors()
{
    // Limpiar líneas viejas
    if (m_cursor1Line) { waveformScene->removeItem(m_cursor1Line); delete m_cursor1Line; m_cursor1Line = nullptr; }
    if (m_cursor2Line) { waveformScene->removeItem(m_cursor2Line); delete m_cursor2Line; m_cursor2Line = nullptr; }

    // Factor de escala
    const double PIXELS_PER_SECOND = 100.0 / waveformTimebase;
    const int maxY = 10000; // Suficiente altura para cubrir todo

    // --- ACTUALIZAR CURSOR 1 ---
    if (m_cursor1Pos.defined) {
        int x1 = static_cast<int>(m_cursor1Pos.timePosition * PIXELS_PER_SECOND);

        // Dibujar línea
        QPen pen(QColor(255, 140, 0)); // Naranja
        pen.setWidth(m_activeCursor == ActiveCursor::C1 ? 3 : 1);
        pen.setStyle(Qt::DashLine);
        m_cursor1Line = waveformScene->addLine(x1, -100, x1, maxY, pen);

        // Actualizar Etiqueta UI
        m_lblC1Info->setText(QString("C1: %1 ms").arg(m_cursor1Pos.timePosition * 1000.0, 0, 'f', 4));
    }
    else {
        m_lblC1Info->setText("C1: --");
    }

    // --- ACTUALIZAR CURSOR 2 ---
    if (m_cursor2Pos.defined) {
        int x2 = static_cast<int>(m_cursor2Pos.timePosition * PIXELS_PER_SECOND);

        // Dibujar línea
        QPen pen(QColor(0, 200, 0)); // Verde
        pen.setWidth(m_activeCursor == ActiveCursor::C2 ? 3 : 1);
        pen.setStyle(Qt::DashLine);
        m_cursor2Line = waveformScene->addLine(x2, -100, x2, maxY, pen);

        // Actualizar Etiqueta UI
        m_lblC2Info->setText(QString("C2: %1 ms").arg(m_cursor2Pos.timePosition * 1000.0, 0, 'f', 4));
    }
    else {
        m_lblC2Info->setText("C2: --");
    }

    // --- ACTUALIZAR DELTA ---
    if (m_cursor1Pos.defined && m_cursor2Pos.defined) {
        double delta = std::abs(m_cursor2Pos.timePosition - m_cursor1Pos.timePosition);

        // Cálculo de frecuencia
        QString freqText;
       

        m_lblDeltaInfo->setText(QString("ΔT: %1 ms%2")
            .arg(delta * 1000.0, 0, 'f', 4)
            .arg(freqText));
    }
    else {
        m_lblDeltaInfo->setText("ΔT: --");
    }
}

bool MainWindow::eventFilter(QObject* obj, QEvent* event)
{
    if (obj == ui->graphicsViewWaveform && event->type() == QEvent::KeyPress) {
        QKeyEvent* keyEvent = static_cast<QKeyEvent*>(event);

        // Solo procesar si hay un cursor activo
        if (m_activeCursor == ActiveCursor::NONE) {
            return QMainWindow::eventFilter(obj, event);
        }

        // LEFT/RIGHT: mover cursor activo a transiciones
        if (keyEvent->key() == Qt::Key_Left || keyEvent->key() == Qt::Key_Right) {
            bool forward = (keyEvent->key() == Qt::Key_Right);
            moveCursorByTransition(forward);
            return true;  // Consumir evento
        }
    }

    return QMainWindow::eventFilter(obj, event);
}

// ============================================================================
// CONTEXT MENU HANDLERS
// ============================================================================

void MainWindow::onPinsTableContextMenu(const QPoint &pos)
{
    // Verificar que hay filas seleccionadas
    QList<QTableWidgetItem*> selectedItems = ui->tableWidgetPins->selectedItems();
    if (selectedItems.isEmpty()) {
        return;  // No mostrar menú si no hay selección
    }

    // Crear menú contextual
    QMenu contextMenu(tr("Pin Actions"), this);

    // Añadir acción "Add to Waveform"
    QAction *addToWaveformAction = new QAction(tr("Add to Waveform"), this);
    addToWaveformAction->setIcon(QIcon::fromTheme("list-add"));
    connect(addToWaveformAction, &QAction::triggered, this, &MainWindow::onWaveformAddSignal);

    contextMenu.addAction(addToWaveformAction);

    // Mostrar menú en la posición del cursor
    contextMenu.exec(ui->tableWidgetPins->mapToGlobal(pos));
}

void MainWindow::onWaveformContextMenu(const QPoint &pos)
{
    // Verificar que hay señales en el waveform
    if (waveformSignals.empty()) {
        return;  // No mostrar menú si no hay señales
    }

    // Convertir posición del widget a coordenadas de escena
    QPointF scenePos = waveformNamesView->mapToScene(pos);

    // Calcular qué señal fue clickeada basándose en la posición Y
    const int SIGNAL_HEIGHT = 30;  // Debe coincidir con el valor en redrawWaveform()
    int row = static_cast<int>(scenePos.y()) / SIGNAL_HEIGHT;

    // Verificar que el row es válido
    if (row < 0 || row >= static_cast<int>(waveformSignals.size())) {
        return;  // Click fuera de las señales
    }

    // Obtener nombre de la señal clickeada
    QString signalName = QString::fromStdString(waveformSignals[row].name);

    // Crear menú contextual
    QMenu contextMenu(tr("Signal Actions"), this);

    // Añadir acción "Remove Signal"
    QAction *removeSignalAction = new QAction(tr("Remove '%1'").arg(signalName), this);
    removeSignalAction->setIcon(QIcon::fromTheme("list-remove"));

    // Conectar la acción para eliminar esta señal específica
    connect(removeSignalAction, &QAction::triggered, this, [this, signalName]() {
        std::string pinName = signalName.toStdString();

        // Eliminar de waveformSignals
        waveformSignals.erase(
            std::remove_if(waveformSignals.begin(), waveformSignals.end(),
                [&pinName](const WaveformSignalInfo& sig) { return sig.name == pinName; }),
            waveformSignals.end());

        // Eliminar del buffer
        waveformBuffer.erase(pinName);

        // Actualizar mensaje de estado
        updateStatusBar(QString("Signal '%1' removed from waveform").arg(signalName));

        // Invalidar cache de transiciones (los cursores pueden necesitar recalcular)
        m_transitionCache.dirty = true;

        // Si no quedan señales, detener el timer y redibujar síncronamente
        if (waveformSignals.empty()) {
            m_waveformRenderTimer->stop();
            m_waveformNeedsRedraw = false;
            redrawWaveform();  // Redibujar síncronamente para limpiar pantalla
        } else {
            // Si quedan señales, solo marcar para redraw asíncrono
            m_waveformNeedsRedraw = true;
        }
    });

    contextMenu.addAction(removeSignalAction);

    // Añadir separador
    contextMenu.addSeparator();

    // Añadir acción "Remove All Signals"
    QAction *removeAllAction = new QAction(tr("Remove All Signals"), this);
    removeAllAction->setIcon(QIcon::fromTheme("edit-clear"));
    connect(removeAllAction, &QAction::triggered, this, &MainWindow::onWaveformRemoveAll);

    contextMenu.addAction(removeAllAction);

    // Mostrar menú en la posición del cursor
    contextMenu.exec(waveformNamesView->mapToGlobal(pos));
}

// ============================================================================
// PROJECT MANAGEMENT (JSON) - IMPLEMENTACIÓN OPTIMIZADA
// ============================================================================

/**
 * @brief Resetea completamente el estado del proyecto actual
 *
 * Limpia todas las variables, buffers y recursos asociados al proyecto
 * para permitir cargar/crear un nuevo proyecto sin reiniciar la aplicación.
 *
 * NO limpia: conexión del adaptador, velocidad, settings de rendimiento
 */
void MainWindow::resetProjectState()
{
    qDebug() << "[resetProjectState] Iniciando limpieza completa del proyecto...";

    // ========================================================================
    // 1. DESCONECTAR SEÑALES Y PARAR BACKEND
    // ========================================================================
    if (scanController) {
        // Desconectar señal de datos para evitar callbacks durante limpieza
        disconnect(scanController.get(), &JTAG::ScanController::pinsDataReady,
            this, &MainWindow::onPinsDataReady);

        if (isCapturing) {
            scanController->stopPolling();
            isCapturing = false;
            ui->actionRun->setText("Run");
        }
    }

    // ========================================================================
    // 2. RESETEAR FLAGS DE ESTADO (excepto conexión del adaptador)
    // ========================================================================
    isCapturing = false;
    isDeviceDetected = false;
    isDeviceInitialized = false;
    isRedrawing = false;
    m_waveformNeedsRedraw = false;
    m_chipVisNeedsRedraw = false;
    m_pinsTableNeedsRedraw = false;
    m_latestPinsData = nullptr;
    m_pendingSamples.clear();  // Limpiar batch de muestras pendientes
    sampleCounter = 0;

    // Resetear modo JTAG a SAMPLE por defecto
    currentJTAGMode = JTAGMode::SAMPLE;

    // ========================================================================
    // 3. LIMPIAR PATHS Y NOMBRES
    // ========================================================================
    currentProjectPath.clear();
    currentBSDLPath.clear();
    customDeviceName.clear();

    // ========================================================================
    // 4. LIMPIAR BUFFERS DE WAVEFORM
    // ========================================================================
    waveformSignals.clear();
    waveformBuffer.clear();
    transitionCounters.clear();
    previousLevels.clear();
    waveformTimebase = 1.0;
    isAutoScrollEnabled = true;

    // Parar timer de waveform
    if (m_waveformRenderTimer && m_waveformRenderTimer->isActive()) {
        m_waveformRenderTimer->stop();
    }

    // Invalidar timer de captura
    captureTimeOffset = 0;
    captureTimer.invalidate();

    // ========================================================================
    // 5. LIMPIAR UI - TABLA DE PINES
    // ========================================================================
    ui->tableWidgetPins->blockSignals(true);
    ui->tableWidgetPins->setRowCount(0);
    ui->tableWidgetPins->blockSignals(false);
    invalidatePinNameCache();  // Invalidar cache de nombres

    // Limpiar combo de dispositivos
    ui->comboBoxDevice->clear();

    // ========================================================================
    // 6. LIMPIAR UI - WAVEFORM
    // ========================================================================
    if (waveformScene) {
        waveformScene->clear();
    }

    // Resetear cursores
    m_cursor1Line = nullptr;
    m_cursor2Line = nullptr;

    // Limpiar scene de timeline si existe
    if (timelineScene) {
        timelineScene->clear();
    }

    // Redibujar waveform vacío
    redrawWaveform();

    // ========================================================================
    // 7. LIMPIAR UI - CHIP VISUALIZER
    // ========================================================================
    m_pendingChipVisUpdates.clear();

    if (chipVisualizer) {
        chipVisualizer->scene()->clear();
        // Forzar procesamiento de eventos para limpiar items gráficos
        QCoreApplication::processEvents();
        QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    }

    // ========================================================================
    // 8. LIMPIAR UI - CONTROL PANEL (Watch)
    // ========================================================================
    if (controlPanel) {
        controlPanel->removeAllPins();
    }
    ui->dockWatch->setVisible(false);

    // ========================================================================
    // 9. RESETEAR UI DE CONTROLES
    // ========================================================================
    ui->actionRun->setText("Run");

    // Resetear radio buttons de modo JTAG
    if (jtagModeButtonGroup && radioSample) {
        jtagModeButtonGroup->blockSignals(true);
        radioSample->setChecked(true);
        jtagModeButtonGroup->blockSignals(false);
    }

    // ========================================================================
    // 10. DESCARGAR BSDL DEL BACKEND (si está cargado)
    // ========================================================================
    if (scanController) {
        scanController->unloadBSDL();
    }

    // Actualizar título de ventana
    updateWindowTitle();

    qDebug() << "[resetProjectState] Limpieza completada.";
}

// Función auxiliar: Crea una ruta relativa
QString MainWindow::makePathRelative(const QString& absolutePath, const QString& basePath) const
{
    QDir baseDir(QFileInfo(basePath).absolutePath());
    return baseDir.relativeFilePath(absolutePath);
}

// Función auxiliar: Reconstruye la ruta absoluta
QString MainWindow::makePathAbsolute(const QString& relativePath, const QString& basePath) const
{
    QDir baseDir(QFileInfo(basePath).absolutePath());
    return baseDir.cleanPath(baseDir.absoluteFilePath(relativePath));
}

bool MainWindow::saveProjectToJson(const QString& filePath)
{
    QJsonObject project;

    // 1. Versión y Metadatos
    project["projectVersion"] = "1.0";
    project["timestamp"] = QDateTime::currentDateTime().toString(Qt::ISODate);

    // 2. Información del dispositivo y BSDL
    if (!currentBSDLPath.isEmpty()) {
        project["bsdlFile"] = makePathRelative(currentBSDLPath, filePath);
    }
    project["deviceName"] = customDeviceName;

    // --- NUEVO: Guardar Configuración del ChipVisualizer ---
    // Vital para que al cargar sepa si es EDGE o CENTER y su tamaño
    if (chipVisualizer && chipVisualizer->scene()) {
        QJsonObject chipConfig;

        // Guardar dimensiones del chip usando getters
        double width = chipVisualizer->getChipWidth();
        double height = chipVisualizer->getChipHeight();

        // Validar que las dimensiones son razonables
        if (width <= 0 || width > 2000) width = 400.0;
        if (height <= 0 || height > 2000) height = 400.0;

        chipConfig["width"] = width;
        chipConfig["height"] = height;
        project["chipDimensions"] = chipConfig;

        // Guardar el tipo de paquete (EDGE_PINS vs CENTER_PINS)
        QString packageType = chipVisualizer->getPackageType();
        if (packageType.isEmpty()) {
            packageType = "CENTER_PINS";  // Default
        }
        project["packageType"] = packageType;

        // Guardar zoom
        project["chipVisualizerZoom"] = chipVisualizer->transform().m11();
    }

    // 3. Configuración del Modo JTAG
    QString jtagModeStr;
    switch (currentJTAGMode) {
    case JTAGMode::SAMPLE: jtagModeStr = "SAMPLE"; break;
    case JTAGMode::SAMPLE_SINGLE_SHOT: jtagModeStr = "SAMPLE_SINGLE_SHOT"; break;
    case JTAGMode::EXTEST: jtagModeStr = "EXTEST"; break;
    case JTAGMode::INTEST: jtagModeStr = "INTEST"; break;
    case JTAGMode::BYPASS: jtagModeStr = "BYPASS"; break;
    }
    project["jtagMode"] = jtagModeStr;

    // 4. Configuración del Waveform
    QJsonObject waveform;
    QJsonArray signalsArray; // Nombre cambiado para evitar conflicto con 'signals' de Qt

    // Guardar SOLO los nombres de las señales (Configuración)
    for (const auto& sig : waveformSignals) {
        signalsArray.append(QString::fromStdString(sig.name));
    }
    waveform["signals"] = signalsArray;

    // Parámetros de visualización
    waveform["timebase"] = waveformTimebase;
    waveform["autoScrollEnabled"] = isAutoScrollEnabled;

    // Guardar posición del scroll
    if (ui->graphicsViewWaveform->horizontalScrollBar()) {
        waveform["scrollPosition"] = ui->graphicsViewWaveform->horizontalScrollBar()->value();
    }

    // NOTA: NO guardamos cursores. Se resetearán al cargar.

    project["waveform"] = waveform;

    // 5. Configuración de Rendimiento (Settings)
    QJsonObject settings;
    settings["pollingInterval"] = currentPollInterval;
    settings["sampleDecimation"] = currentSampleDecimation;
    settings["samplesPerSecond"] = currentSamplesPerSecond;
    settings["waveformFPS"] = currentWaveformFPS;
    settings["chipVisFPS"] = currentChipVisFPS;
    project["settings"] = settings;

    // 6. Disposición de Ventanas (Window Layout)
    QJsonObject layout;
    layout["geometry"] = QString(saveGeometry().toBase64());
    layout["windowState"] = QString(saveState().toBase64());

    // Guardar anchos de columnas de la tabla de pines
    if (ui->tableWidgetPins->columnCount() > 0) {
        QJsonArray columnWidths;
        for (int i = 0; i < ui->tableWidgetPins->columnCount(); ++i) {
            columnWidths.append(ui->tableWidgetPins->columnWidth(i));
        }
        layout["pinsTableColumnWidths"] = columnWidths;
    }
    project["layout"] = layout;

    // 7. Nombres Personalizados de Pines
    // Guardamos pares (pinNumber -> customName) para poder restaurarlos
    // incluso si el BSDL tiene nombres originales diferentes
    if (scanController && scanController->getDeviceModel()) {
        QJsonObject customNames;
        const auto& allPins = scanController->getDeviceModel()->getAllPins();

        for (const auto& pin : allPins) {
            // Usar pinNumber como key inmutable
            // Guardar el nombre actual (que puede ser personalizado)
            customNames[QString::fromStdString(pin.pinNumber)] = QString::fromStdString(pin.name);
        }

        project["customPinNames"] = customNames;
    }

    // --- GUARDADO EN DISCO ---
    QJsonDocument doc(project);
    QFile file(filePath);
    if (!file.open(QIODevice::WriteOnly)) {
        QMessageBox::critical(this, "Save Error",
            QString("Could not save project file:\n%1").arg(file.errorString()));
        return false;
    }

    file.write(doc.toJson(QJsonDocument::Indented));
    file.close();

    currentProjectPath = filePath;
    updateStatusBar(QString("Project saved: %1").arg(QFileInfo(filePath).fileName()));
    return true;
}

bool MainWindow::loadProjectFromJson(const QString& filePath)
{
    // ======================================================================
    // FLUJO IDÉNTICO A: onNewProjectWizard() + onDeviceBSDLFile()
    // Sin congelar UI, sin timers, flujo lineal directo
    // ======================================================================

    // 0. VERIFICAR REQUISITOS (igual que onDeviceBSDLFile)
    if (!isAdapterConnected) {
        QMessageBox::warning(this, "Not Connected",
            "Please connect to JTAG adapter first");
        return false;
    }

    // 1. LEER JSON
    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly)) {
        QMessageBox::critical(this, "Load Error",
            QString("Could not open project file:\n%1").arg(file.errorString()));
        return false;
    }

    QJsonDocument doc = QJsonDocument::fromJson(file.readAll());
    file.close();

    if (doc.isNull() || !doc.isObject()) {
        QMessageBox::critical(this, "Load Error", "Invalid project file format");
        return false;
    }

    QJsonObject project = doc.object();

    // Verificar que hay BSDL
    QString relativeBSDLPath = project["bsdlFile"].toString();
    if (relativeBSDLPath.isEmpty()) {
        QMessageBox::warning(this, "Invalid Project", "Project file has no BSDL reference");
        return false;
    }

    QString absoluteBSDLPath = makePathAbsolute(relativeBSDLPath, filePath);
    if (!QFile::exists(absoluteBSDLPath)) {
        QMessageBox::warning(this, "BSDL Missing", "BSDL file not found:\n" + absoluteBSDLPath);
        return false;
    }

    // ======================================================================
    // 2. RESETEAR ESTADO COMPLETO DEL PROYECTO ANTERIOR
    // ======================================================================
    resetProjectState();

    // ======================================================================
    // 4. CONFIGURAR DESDE JSON (CON CORRECCIÓN DE EVENT LOOP STARVATION)
    // ======================================================================

    // Settings de rendimiento
    if (project.contains("settings")) {
        QJsonObject settings = project["settings"].toObject();

        // --- CORRECCIÓN 1: SANITIZACIÓN DE INTERVALO ---
        // 1. Priorizar samplesPerSecond sobre pollingInterval para evitar incoherencias
        currentSamplesPerSecond = settings["samplesPerSecond"].toInt(10);

        // 2. Calcular intervalo basado en muestras (e.g. 1000ms / 30Hz = 33ms)
        // Esto ignora el "1" corrupto del JSON y usa el valor correcto derivado de la frecuencia.
        int calculatedInterval = 1000 / (currentSamplesPerSecond > 0 ? currentSamplesPerSecond : 1);

        // 3. Leer pollingInterval pero IMPONER LÍMITE DE SEGURIDAD (Mínimo 10ms = 100Hz máx)
        // Esto protege contra archivos corruptos que pidan 1ms (1000Hz) y saturen la cola de eventos.
        int rawInterval = settings["pollingInterval"].toInt(100);
        currentPollInterval = std::max(10, rawInterval);

        // 4. Si hay discrepancia grave, preferir el cálculo basado en Hz (que es lo que ve el usuario)
        if (std::abs(currentPollInterval - calculatedInterval) > 5) {
            qDebug() << "[Load] Fixing polling interval mismatch. JSON raw:" << rawInterval
                << "Calculated from Hz:" << calculatedInterval;
            currentPollInterval = calculatedInterval;
        }
        // -----------------------------------------------

        currentSampleDecimation = settings["sampleDecimation"].toInt(1);
        currentWaveformFPS = settings["waveformFPS"].toInt(30);
        currentChipVisFPS = settings["chipVisFPS"].toInt(10);

        if (scanController) {
            scanController->setPollInterval(currentPollInterval);
            scanController->setSamplesPerSecond(currentSamplesPerSecond);
        }
        if (m_waveformRenderTimer) m_waveformRenderTimer->setInterval(1000 / currentWaveformFPS);
        if (m_chipVisRenderTimer) m_chipVisRenderTimer->setInterval(1000 / currentChipVisFPS);
    }

    // Configurar ChipVisualizer
    if (chipVisualizer) {
        if (project.contains("packageType")) {
            chipVisualizer->setPackageType(project["packageType"].toString());
        }
        if (project.contains("chipDimensions")) {
            QJsonObject dims = project["chipDimensions"].toObject();
            chipVisualizer->setCustomDimensions(dims["width"].toDouble(), dims["height"].toDouble());
        }
    }

    customDeviceName = project["deviceName"].toString();

    // ======================================================================
    // 5. CARGAR BSDL
    // ======================================================================
#ifdef _WIN32
    std::filesystem::path bsdlPath(absoluteBSDLPath.toStdWString());
#else
    std::filesystem::path bsdlPath(absoluteBSDLPath.toStdString());
#endif

    if (scanController->loadBSDL(bsdlPath)) {
        updateStatusBar("BSDL loaded: " + absoluteBSDLPath);
        currentBSDLPath = absoluteBSDLPath;

        if (scanController->initializeDevice()) {
            isDeviceInitialized = true;
            isDeviceDetected = true;

            // Aplicar nombres personalizados ANTES de updatePinsTable()
            // El JSON guarda: pinNumber -> nombrePersonalizado
            // Necesitamos buscar cada pin por su pinNumber y renombrar si es diferente
            if (project.contains("customPinNames")) {
                QJsonObject customNames = project["customPinNames"].toObject();
                auto* model = scanController->getDeviceModel();

                // Iterar todos los pines del modelo recién cargado
                const auto& allPins = model->getAllPins();
                for (const auto& pin : allPins) {
                    QString pinNumber = QString::fromStdString(pin.pinNumber);

                    // Buscar si hay un nombre personalizado para este pinNumber
                    if (customNames.contains(pinNumber)) {
                        QString savedName = customNames[pinNumber].toString();
                        QString currentName = QString::fromStdString(pin.name);

                        // Solo renombrar si el nombre guardado es diferente del actual
                        if (savedName != currentName && !savedName.isEmpty()) {
                            model->renamePinAlias(currentName.toStdString(), savedName.toStdString());
                        }
                    }
                }
            }

            // ======================================================================
            // 6. RECONECTAR TODO
            // ======================================================================
            updatePinsTable();
            renderChipVisualization();

            connect(scanController.get(), &JTAG::ScanController::pinsDataReady,
                this, &MainWindow::onPinsDataReady);

            // ======================================================================
            // 7. ENTRAR EN MODO JTAG
            // ======================================================================
            int modeIdx = 0;
            if (project.contains("jtagMode")) {
                QString modeStr = project["jtagMode"].toString();
                if (modeStr == "EXTEST") modeIdx = 2;
                else if (modeStr == "INTEST") modeIdx = 3;
                else if (modeStr == "BYPASS") modeIdx = 4;
                else if (modeStr == "SAMPLE_SINGLE_SHOT") modeIdx = 1;
            }
            currentJTAGMode = static_cast<JTAGMode>(modeIdx);

            // Ejecutar secuencia IEEE 1149.1 según el modo
            bool modeSuccess = false;
            switch (currentJTAGMode) {
            case JTAGMode::EXTEST:
                modeSuccess = scanController->enterEXTEST();
                break;
            case JTAGMode::INTEST:
                modeSuccess = scanController->enterINTEST();
                break;
            case JTAGMode::BYPASS:
                modeSuccess = scanController->enterBYPASS();
                break;
            case JTAGMode::SAMPLE:
            case JTAGMode::SAMPLE_SINGLE_SHOT:
            default:
                modeSuccess = scanController->enterSAMPLE();
                break;
            }

            if (modeSuccess && currentJTAGMode != JTAGMode::BYPASS) {
                isCapturing = true;
                captureTimer.start();

                // --- CORRECCIÓN 2: NO INICIAR POLLING AQUÍ ---
                // scanController->startPolling();  <-- ELIMINADO: Causaba conflicto con la carga de UI

                updateStatusBar("Project loaded - " +
                    QString(currentJTAGMode == JTAGMode::SAMPLE ? "SAMPLE" :
                        (currentJTAGMode == JTAGMode::EXTEST ? "EXTEST" : "INTEST")) + " mode active");
                ui->actionRun->setText("Stop");
            }

            // ======================================================================
            // 8. FINALIZAR
            // ======================================================================
            enableControlsAfterConnection(true);
            onJTAGModeChanged(modeIdx);

            // Actualizar radio buttons sin disparar señales
            if (jtagModeButtonGroup) {
                jtagModeButtonGroup->blockSignals(true);
                if (QAbstractButton* btn = jtagModeButtonGroup->button(modeIdx)) {
                    btn->setChecked(true);
                }
                jtagModeButtonGroup->blockSignals(false);
            }

            // ======================================================================
            // 9. RESTAURAR WAVEFORM
            // ======================================================================
            if (project.contains("waveform")) {
                QJsonObject waveform = project["waveform"].toObject();
                waveformTimebase = waveform["timebase"].toDouble(1.0);
                isAutoScrollEnabled = waveform["autoScrollEnabled"].toBool(true);

                QJsonArray jsonSignals = waveform["signals"].toArray();
                for (const auto& val : jsonSignals) {
                    QString pinName = val.toString();
                    auto pinInfo = scanController->getDeviceModel()->getPinInfo(pinName.toStdString());
                    if (pinInfo) {
                        WaveformSignalInfo sigInfo;
                        sigInfo.name = pinName.toStdString();
                        if (pinInfo->inputCell != -1) sigInfo.dataIndex = pinInfo->inputCell;
                        else if (pinInfo->outputCell != -1) sigInfo.dataIndex = pinInfo->outputCell;
                        else sigInfo.dataIndex = -1;

                        waveformSignals.push_back(sigInfo);
                        waveformBuffer[sigInfo.name].clear();
                    }
                }

                if (!waveformSignals.empty() && m_waveformRenderTimer) {
                    m_waveformRenderTimer->start();
                }
                m_waveformNeedsRedraw = true;
                redrawWaveform();
            }

            // ======================================================================
            // 10. RESTAURAR LAYOUT
            // ======================================================================
            if (project.contains("layout")) {
                QJsonObject layout = project["layout"].toObject();
                if (layout.contains("geometry")) {
                    restoreGeometry(QByteArray::fromBase64(layout["geometry"].toString().toLatin1()));
                }
                if (layout.contains("windowState")) {
                    restoreState(QByteArray::fromBase64(layout["windowState"].toString().toLatin1()));
                }
                if (layout.contains("pinsTableColumnWidths")) {
                    QJsonArray columnWidths = layout["pinsTableColumnWidths"].toArray();
                    for (int i = 0; i < columnWidths.size() && i < ui->tableWidgetPins->columnCount(); ++i) {
                        ui->tableWidgetPins->setColumnWidth(i, columnWidths[i].toInt());
                    }
                }
            }

            // Restaurar zoom del chip
            if (project.contains("chipVisualizerZoom") && chipVisualizer) {
                double zoom = project["chipVisualizerZoom"].toDouble(1.0);
                chipVisualizer->resetTransform();
                chipVisualizer->scale(zoom, zoom);
            }

            // ======================================================================
            // PASO FINAL: INICIAR POLLING DE FORMA SEGURA
            // ======================================================================
            // Diferimos el inicio del worker para permitir que Qt termine de procesar 
            // los eventos de redimensionado y layout de la carga del proyecto.
            if (isCapturing) {
                QTimer::singleShot(100, this, [this]() {
                    if (scanController && isCapturing) {
                        scanController->startPolling();
                        qDebug() << "[ProjectLoad] Polling started safely.";
                    }
                    else {
                        qWarning() << "[ProjectLoad] Polling skipped: Controller null or Capture false";
                    }
                    });
            }

            currentProjectPath = filePath;
            return true;
        }
    }

    // Si llegamos aquí, algo falló - reconectar señal
    connect(scanController.get(), &JTAG::ScanController::pinsDataReady,
        this, &MainWindow::onPinsDataReady);
    QMessageBox::critical(this, "Error", "Failed to load BSDL file");
    return false;
}

void MainWindow::onSaveProject()
{
    QString filePath = currentProjectPath;

    if (filePath.isEmpty()) {
        filePath = QFileDialog::getSaveFileName(this,
            tr("Save Project"), "",
            tr("JTAG Scanner Project (*.jsqp);;All Files (*)"));

        if (filePath.isEmpty()) {
            return;
        }

        if (!filePath.endsWith(".jsqp", Qt::CaseInsensitive)) {
            filePath += ".jsqp";
        }
    }

    saveProjectToJson(filePath);
}

void MainWindow::onOpenProject()
{
    QString filePath = QFileDialog::getOpenFileName(this,
        tr("Open Project"), "",
        tr("JTAG Scanner Project (*.jsqp);;All Files (*)"));

    if (filePath.isEmpty()) {
        return;
    }

    loadProjectFromJson(filePath);
}

