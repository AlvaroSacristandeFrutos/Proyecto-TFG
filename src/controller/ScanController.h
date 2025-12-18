#pragma once

#include <memory>
#include <string>
#include <vector>
#include <map>
#include <filesystem>
#include <QObject>
#include <QThread>

#include "../core/BoundaryScanEngine.h"
#include "../bsdl/DeviceModel.h"
#include "../hal/IJTAGAdapter.h"       // Define AdapterDescriptor
#include "../hal/factory/AdapterFactory.h"
#include "ScanWorker.h"

namespace JTAG {

    class ScanController : public QObject {
        Q_OBJECT
    public:
        ScanController();
        ~ScanController();

        ScanController(const ScanController&) = delete;
        ScanController& operator=(const ScanController&) = delete;
        ScanController(ScanController&&) noexcept = default;
        ScanController& operator=(ScanController&&) noexcept = default;
        void setScanMode(ScanMode mode);
        // --- NUEVO: Método para obtener sondas detectadas ---
        std::vector<AdapterDescriptor> getDetectedAdapters() const;

        // Gestión de Adaptador
        bool connectAdapter(AdapterType type, uint32_t clockSpeed = 1000000);
        bool connectAdapter(const AdapterDescriptor& descriptor, uint32_t clockSpeed);
        void disconnectAdapter();
        void unloadBSDL();  // NUEVO: Descarga solo el BSDL sin desconectar el adaptador
        bool isConnected() const;
        std::string getAdapterInfo() const;

        // Gestión de Dispositivo
        uint32_t detectDevice();
        bool loadBSDL(const std::filesystem::path& bsdlPath);
        std::string getDeviceName() const;
        std::string getPackageInfo() const;

        // Control
        bool initialize();
        bool reset();

        // Pines
        bool setPin(const std::string& pinName, PinLevel level);
        std::optional<PinLevel> getPin(const std::string& pinName) const;
        std::vector<std::string> getPinList() const;
        bool applyChanges();
        bool samplePins();

        // Información adicional de pines
        std::string getPinPort(const std::string& pinName) const;
        std::string getPinType(const std::string& pinName) const;
        std::string getPinNumber(const std::string& pinName) const;

        // Avanzado
        bool setPins(const std::map<std::string, PinLevel>& pins);
        std::map<std::string, PinLevel> getPins(const std::vector<std::string>& pinNames) const;
        bool runTest(size_t numCycles);

        uint32_t getIDCODE() const { return detectedIDCODE; }
        bool isInitialized() const { return initialized; }

        bool enterSAMPLE();
        bool enterEXTEST();
        bool enterBYPASS();
        bool enterINTEST();

        // NUEVO: Cambiar modo de operación del engine
        void setEngineOperationMode(BoundaryScanEngine::OperationMode mode);

        // Operaciones de Bus
        bool writeBus(const std::vector<std::string>& pinNames, uint32_t value);

        // Carga de modelo (Simulada o Real)
        bool loadDeviceModel(const std::string& path = ""); // path opcional para el stub de prueba
        bool initializeDevice();

        // NUEVO: Exponer DeviceModel para visualización
        const DeviceModel* getDeviceModel() const { return deviceModel.get(); }

        // Target detection - check if BSR shows no target (all 0xFF)
        bool isNoTargetDetected() const;

        // Threading control
        void startPolling();
        void stopPolling();
        void setPollInterval(int ms);

        // Thread-safe pin control (marca como dirty sin bloquear)
        void setPinAsync(const std::string& pinName, PinLevel level);

    signals:
        // Señales para la GUI (emitidas por el worker, re-emitidas por controller)
        void pinsDataReady(std::vector<PinLevel> pins);
        void errorOccurred(QString message);

    private slots:
        // Slots para recibir señales del worker y re-emitirlas
        void onPinsUpdated(std::vector<PinLevel> pins);
        void onWorkerError(QString message);
        void onWorkerStopped();  // Handle worker stop (for single-shot)

    private:
        // Helper methods
        void createMockDeviceModel();  // Auto-genera modelo para MockAdapter

        std::unique_ptr<IJTAGAdapter> adapter;
        std::unique_ptr<BoundaryScanEngine> engine;
        std::unique_ptr<DeviceModel> deviceModel;

        // Threading
        QThread* workerThread = nullptr;
        ScanWorker* scanWorker = nullptr;
        int pollIntervalMs = 100;

        uint32_t detectedIDCODE;
        bool initialized;
    };

} // namespace JTAG

// Nota: std::vector<JTAG::PinLevel> ya está registrado en ScanWorker.h
// pero lo declaramos aquí también por si se incluye este header independientemente