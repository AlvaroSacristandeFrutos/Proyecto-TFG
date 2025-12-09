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
    void onOpen();
    void onSave();
    void onSaveAs();
    void onExit();

    // View menu actions
    void onTogglePins(bool checked);
    void onToggleWatch(bool checked);
    void onToggleWaveform(bool checked);
    void onZoom();
    void onInoutPinsDisplaying(bool isIN);

    // Scan menu actions
    void onJTAGConnection();
    void onExamineChain();
    void onRun();
    void onJTAGReset();
    void onDeviceInstruction();
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
    void onWatchAddSignal();
    void onWatchRemove();
    void onWatchRemoveAll();
    void onWatchZeroTransitionCounter();
    void onWatchZeroAllTransitionCounters();

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
    void onWaveformPreviousEvent();
    void onWaveformNextEvent();

    // Help menu actions
    void onHelpContents();
    void onTurnOnLogging();
    void onRegister();
    void onAbout();

    // Toolbar actions
    void onInstruction();

    // Pins panel actions
    void onDeviceChanged(int index);
    void onSearchPinsButton();
    void onPinTableSelectionChanged();  // NEW: Highlight pin in visualizer
    void onPinTableItemChanged(QTableWidgetItem* item);  // NEW: Handle pin name changes

    // Waveform toolbar actions (internal waveform controls)
    void onWaveZoomIn();
    void onWaveZoomOut();
    void onWaveFit();
    void onWavePrev();
    void onWaveNext();
    void onWaveGoto();

    // NUEVOS slots para recibir datos del worker
    void onPinsDataReady(std::vector<JTAG::PinLevel> pins);
    void onScanError(QString message);

private:
    Ui::MainWindow *ui;
    
    // Backend controller - AQUÍ CONECTARÁS TU SCANCONTROLLER
    std::unique_ptr<JTAG::ScanController> scanController;

    // Graphics scenes for rendering
    QGraphicsScene *waveformScene;

    // Chip visualization
    ChipVisualizer *chipVisualizer;
    
    // Toolbar widgets
    QComboBox *zoomComboBox;

    // Action group for IN/OUT of inout radio buttons
    QActionGroup *inoutActionGroup;

    // Current zoom level
    double currentZoom;
    
    // Connection state
    bool isAdapterConnected;
    bool isDeviceDetected;
    bool isDeviceInitialized;
    
    // Waveform capture state
    bool isCapturing;
    double waveformTimebase; // in seconds

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
    void updateWatchTable();
    void captureWaveformSample();
    void redrawWaveform();
    void enableControlsAfterConnection(bool enable);
    void renderChipVisualization();

    // Pin name resolution helper
    QString resolveRealPinName(const QString& displayName) const;
};

#endif // MAINWINDOW_H
