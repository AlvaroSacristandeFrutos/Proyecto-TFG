#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>
#include <QToolBar>
#include <QStatusBar>
#include <QDockWidget>
#include <QAction>
#include <QLabel>
#include <QTimer>
#include <memory>

// --- CORRECCIÓN CRÍTICA ---
// Incluimos el fichero directamente para que el compilador vea la clase completa
// y sepa que pertenece al namespace JTAG.
#include "../controller/ScanController.h"

// Forward declarations de las clases de GUI (estas sí pueden quedarse así)
class ChipVisualizer;
class PinControlPanel;

class MainWindow : public QMainWindow {
    Q_OBJECT

public:
    explicit MainWindow(QWidget* parent = nullptr);
    ~MainWindow() override;

private slots:
    void onConnectAdapter();
    void onDisconnect();
    void onEnterSample();
    void onEnterExtest();
    void onEnterBypass();
    void onLoadBSDL();
    void onInitializeDevice();
    void onResetTAP();
    void onPinChanged(const QString& pinName, bool level);
    void onBusWrite(const QStringList& pins, uint32_t value);
    void onRefreshPins();
    void onAutoRefreshToggled(bool enabled);
    void onAbout();

private:
    void createActions();
    void createMenus();
    void createToolBars();
    void createDockWidgets();
    void createStatusBar();
    void connectBackend();
    void updateConnectionStatus();
    void updateDeviceInfo();
    void updatePinStates();

    ChipVisualizer* m_chipVisualizer;
    PinControlPanel* m_pinControlPanel;

    // --- CORRECCIÓN: Usamos JTAG::ScanController explícitamente ---
    std::unique_ptr<JTAG::ScanController> m_controller;

    QToolBar* m_mainToolBar;
    QToolBar* m_modeToolBar;

    QAction* m_actConnect;
    QAction* m_actDisconnect;
    QAction* m_actLoadBSDL;
    QAction* m_actInitDevice;
    QAction* m_actSample;
    QAction* m_actExtest;
    QAction* m_actBypass;
    QAction* m_actResetTAP;
    QAction* m_actRefresh;
    QAction* m_actAutoRefresh;
    QAction* m_actAbout;

    QLabel* m_statusConnection;
    QLabel* m_statusDevice;
    QLabel* m_statusMode;

    bool m_connected;
    bool m_deviceInitialized;
    QString m_currentMode;
    QTimer* m_refreshTimer;
};

#endif // MAINWINDOW_H