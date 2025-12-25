#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>
#include <QGraphicsScene>
#include <QGraphicsView>
#include <QComboBox>
#include <QLabel>
#include <QActionGroup>
#include <QElapsedTimer>
#include <QTableWidgetItem>
#include <memory>
#include <deque>
#include <map>
#include <string>
#include <vector>

#include "ChipVisualizer.h"
#include "ControlPanelWidget.h"

// Forward declarations for your backend
namespace JTAG {
    class ScanController;
    enum class PinLevel;
}

QT_BEGIN_NAMESPACE
namespace Ui {
    class MainWindow;
}
QT_END_NAMESPACE

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    MainWindow(QWidget *parent = nullptr);
    ~MainWindow();

private slots:
    // File menu actions
    void onNewProjectWizard();
    void onExit();

    // View menu actions
    void onTogglePins(bool checked);
    void onToggleWatch(bool checked);
    void onToggleWaveform(bool checked);
    void onZoom();
    void onSettings();                          // Open settings dialog
    void onPollingIntervalChanged(int ms);      // Handle polling interval change
    void onSampleDecimationChanged(int decimation);  // Handle sample decimation change

    // Scan menu actions
    void onJTAGConnection();
    void onExamineChain();
    void onRun();
    void onReset();
    void onJTAGReset();
    void onDeviceBSDLFile();
    void onDevicePackage();
    void onDeviceProperties();

    // Pins menu actions
    void onSearchPins();
    void onEditPinNamesAndBuses();
    void onSetTo0();
    void onSetTo1();
    void onSetToZ();
    void onTogglePinValue();
    void onSetBusValue();
    void onSetBusToAllZ();
    void onSetAllDevicePinsToBSDLSafe();

    // Watch menu actions
    void onWatchShow();

    // Waveform menu actions
    void onWaveformClose();
    void onWaveformAddSignal();
    void onWaveformRemove();
    void onWaveformRemoveAll();
    void onWaveformClear();
    void onWaveformZoom();
    void onWaveformZoomIn();
    void onWaveformZoomOut();
    void onWaveformGoToTime();

    // Help menu actions
    void onHelpContents();
    void onAbout();

    // Toolbar actions
    // void onInstruction();  // Removed - use onDeviceInstruction() instead

    // Pins panel actions
    void onDeviceChanged(int index);
    void onSearchPinsButton();
    void onPinTableSelectionChanged();  // NEW: Highlight pin in visualizer
    void onPinTableItemChanged(QTableWidgetItem* item);  // NEW: Handle pin name changes

    // Waveform toolbar actions (internal waveform controls)
    void onWaveZoomIn();
    void onWaveZoomOut();
    void onWaveFit();
    void onWaveGoto();

    // NUEVOS slots para recibir datos del worker
    // FASE 2: shared_ptr evita copias innecesarias del vector completo
    void onPinsDataReady(std::shared_ptr<const std::vector<JTAG::PinLevel>> pins);
    void onScanError(QString message);

    // JTAG Mode selection slots
    void onJTAGModeChanged(int modeId);

    // Quick action button slots
    void onSetAllToSafeState();
    void onSetAllTo1();
    void onSetAllToZ();
    void onSetAllTo0();

    // Control Panel slot
    void onControlPanelPinChanged(QString pinName, JTAG::PinLevel level);

private:
    Ui::MainWindow *ui;
    
    // Backend controller - AQUÍ CONECTARÁS TU SCANCONTROLLER
    std::unique_ptr<JTAG::ScanController> scanController;

    // Graphics scenes for rendering
    QGraphicsScene *waveformScene;
    QGraphicsScene *waveformNamesScene;  // Scene para nombres de señales (fija)
    QGraphicsScene *timelineScene;  // Timeline separada (parte superior)
    QGraphicsView *timelineView;    // Vista para la timeline
    QGraphicsView *waveformNamesView;  // Vista para nombres (fija, sin scroll horizontal)

    // Chip visualization
    ChipVisualizer *chipVisualizer;

    // Control Panel (reemplaza Watch)
    ControlPanelWidget *controlPanel;

    // Toolbar widgets
    QComboBox *zoomComboBox;

    // JTAG Mode selector widgets
    class QRadioButton *radioSample;
    class QRadioButton *radioSampleSingleShot;
    class QRadioButton *radioExtest;
    class QRadioButton *radioIntest;
    class QRadioButton *radioBypass;
    class QButtonGroup *jtagModeButtonGroup;

    // Quick action buttons
    class QPushButton *btnSetAllSafe;
    class QPushButton *btnSetAll1;
    class QPushButton *btnSetAllZ;
    class QPushButton *btnSetAll0;

    // Current zoom level
    double currentZoom;
    
    // Connection state
    bool isAdapterConnected;
    bool isDeviceDetected;
    bool isDeviceInitialized;

    // Device configuration from wizard
    QString customDeviceName;

    // JTAG Mode state
    enum class JTAGMode { SAMPLE, SAMPLE_SINGLE_SHOT, EXTEST, INTEST, BYPASS };
    JTAGMode currentJTAGMode;
    
    // Waveform capture state
    bool isCapturing;
    double waveformTimebase; // in seconds
    bool isRedrawing; // Flag para prevenir redibujado recursivo
    bool isAutoScrollEnabled; // Flag para controlar auto-scroll en modo captura

    // Transition counters for Watch
    std::map<std::string, int> transitionCounters;  // pinName -> count
    std::map<std::string, JTAG::PinLevel> previousLevels;  // For edge detection

    // Waveform capture structures
    struct WaveformSample {
        double timestamp;        // Seconds since capture start
        JTAG::PinLevel level;
    };
    std::map<std::string, std::deque<WaveformSample>> waveformBuffer;
    QElapsedTimer captureTimer;
    const size_t MAX_WAVEFORM_SAMPLES = 10000;  // Circular buffer limit

    // Performance settings
    int currentPollInterval = 100;      // Polling interval in ms (default: 100ms)
    int currentSampleDecimation = 1;    // Sample decimation (1 = all samples)
    int sampleCounter = 0;              // Counter for sample decimation

    // ===== OPTIMIZACIÓN: Cache de índices directos para waveform =====
    // En lugar de buscar getPinInfo() en cada sample (20+ búsquedas @ 50Hz),
    // guardamos el índice BSR directo UNA VEZ cuando se añade la señal
    struct WaveformSignalInfo {
        std::string name;
        int dataIndex;   // Índice directo en vector BSR (inputCell o outputCell)
                        // -1 si el pin no tiene celdas JTAG
    };
    std::vector<WaveformSignalInfo> waveformSignals;  // Cache de señales con índices
    // =================================================================

    // ===== RENDER THROTTLING: Desacople captura vs. renderizado =====
    // Solución para Event Loop Starvation con polling ultra-rápido (1ms)
    QTimer* m_waveformRenderTimer;      // Timer @ 30 FPS para redraw
    bool m_waveformNeedsRedraw;         // Bandera dirty para redraw pendiente
    // ================================================================

    QLabel* waveformZoomLabel;  // Zoom indicator in toolbar

    // Helper methods
    void setupConnections();
    void setupToolbar();
    void setupGraphicsViews();
    void setupTables();
    void setupBackend();
    void initializeUI();
    void updateWindowTitle(const QString &filename = QString());
    void updateStatusBar(const QString &message);

    // Backend integration helpers
    void updatePinsTable();
    void updateControlPanel(const std::vector<JTAG::PinLevel>& pinLevels);
    void captureWaveformSample(const std::vector<JTAG::PinLevel>& currentPins);
    void redrawWaveform();
    void enableControlsAfterConnection(bool enable);
    void renderChipVisualization();

    // Pin name resolution helper
    QString resolveRealPinName(const QString& displayName) const;

    // Window state persistence (geometry, docks, splitters)
    void saveWindowState();
    void loadWindowState();

    // Mode validation helper
    bool isEditingModeActive();
};

#endif // MAINWINDOW_H
