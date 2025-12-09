#pragma once

#include <QObject>
#include <QThread>
#include <atomic>
#include <vector>
#include <mutex>
#include <map>
#include "../core/BoundaryScanEngine.h"

namespace JTAG {

class ScanWorker : public QObject {
    Q_OBJECT

public:
    explicit ScanWorker(BoundaryScanEngine* engine, QObject* parent = nullptr);
    ~ScanWorker();

    // Control desde GUI thread
    void start();
    void stop();
    void setPollInterval(int ms);

    // Thread-safe: GUI puede marcar pines como dirty
    void markDirtyPin(size_t cellIndex, PinLevel level);
    bool hasDirtyPins() const;

signals:
    void pinsUpdated(std::vector<PinLevel> pins);
    void errorOccurred(QString message);
    void started();
    void stopped();

public slots:
    void run();  // Loop principal del worker

private:
    void processDirtyPins();
    void switchToEXTEST();
    void switchToSAMPLE();

    BoundaryScanEngine* engine;
    std::atomic<bool> running{false};
    std::atomic<int> pollIntervalMs{100};

    // Estado dirty thread-safe
    mutable std::mutex dirtyMutex;
    std::map<size_t, PinLevel> dirtyPins;  // cellIndex → nuevo valor

    bool inEXTESTMode{false};
};

} // namespace JTAG
