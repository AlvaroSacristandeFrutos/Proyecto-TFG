#pragma once

#include <QObject>
#include <QThread>
#include <atomic>
#include <vector>
#include <mutex>
#include <map>
#include "../core/BoundaryScanEngine.h"
#include "../bsdl/DeviceModel.h"

namespace JTAG {

    // Definimos los modos posibles
    enum class ScanMode {
        SAMPLE,
        EXTEST,
        INTEST,
        BYPASS
    };

    class ScanWorker : public QObject {
        Q_OBJECT

    public:
        explicit ScanWorker(BoundaryScanEngine* engine, DeviceModel* model, QObject* parent = nullptr);
        ~ScanWorker();

        void start();
        void stop();
        void setPollInterval(int ms);

        // Nuevo: Control de Modo explícito
        void setScanMode(ScanMode mode);

        // Thread-safe: GUI puede marcar pines como dirty
        void markDirtyPin(size_t cellIndex, PinLevel level);

        // Método auxiliar para saber si hay cambios pendientes
        bool hasDirtyPins() const;

    signals:
        void pinsUpdated(std::vector<PinLevel> pins);
        void errorOccurred(QString message);
        void started();
        void stopped();

    public slots:
        void run();

    private:
        void processDirtyPins();


        // Funciones de conmutación de bajo nivel
        void applyMode(ScanMode mode);

        BoundaryScanEngine* engine;
        DeviceModel* deviceModel;

        std::atomic<bool> running{ false };
        std::atomic<int> pollIntervalMs{ 100 };

        // Estado del modo deseado (Atómico para thread-safety)
        std::atomic<ScanMode> currentMode{ ScanMode::SAMPLE };
        ScanMode lastAppliedMode{ ScanMode::SAMPLE }; // Para detectar cambios

        mutable std::mutex dirtyMutex;
        std::map<size_t, PinLevel> dirtyPins;
        std::map<size_t, PinLevel> desiredOutputs;
    };

} // namespace JTAG

Q_DECLARE_METATYPE(std::vector<JTAG::PinLevel>)