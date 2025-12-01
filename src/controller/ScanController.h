#pragma once

#include <memory>
#include <string>
#include <vector>
#include <map>

#include "../core/BoundaryScanEngine.h"
#include "../bsdl/DeviceModel.h"
#include "../hal/IJTAGAdapter.h"       // Define AdapterDescriptor
#include "../hal/factory/AdapterFactory.h"

namespace JTAG {

    class ScanController {
    public:
        ScanController();
        ~ScanController();

        ScanController(const ScanController&) = delete;
        ScanController& operator=(const ScanController&) = delete;
        ScanController(ScanController&&) noexcept = default;
        ScanController& operator=(ScanController&&) noexcept = default;

        // --- NUEVO: Método para obtener sondas detectadas ---
        std::vector<AdapterDescriptor> getDetectedAdapters() const;

        // Gestión de Adaptador
        bool connectAdapter(AdapterType type, uint32_t clockSpeed = 1000000);
        void disconnectAdapter();
        bool isConnected() const;
        std::string getAdapterInfo() const;

        // Gestión de Dispositivo
        uint32_t detectDevice();
        bool loadBSDL(const std::string& bsdlPath);
        std::string getDeviceName() const;

        // Control
        bool initialize();
        bool reset();

        // Pines
        bool setPin(const std::string& pinName, PinLevel level);
        std::optional<PinLevel> getPin(const std::string& pinName) const;
        std::vector<std::string> getPinList() const;
        bool applyChanges();
        bool samplePins();

        // Avanzado
        bool setPins(const std::map<std::string, PinLevel>& pins);
        std::map<std::string, PinLevel> getPins(const std::vector<std::string>& pinNames) const;
        bool runTest(size_t numCycles);

        uint32_t getIDCODE() const { return detectedIDCODE; }
        bool isInitialized() const { return initialized; }

        bool enterSAMPLE();
        bool enterEXTEST();
        bool enterBYPASS();

        // Operaciones de Bus
        bool writeBus(const std::vector<std::string>& pinNames, uint32_t value);

        // Carga de modelo (Simulada o Real)
        bool loadDeviceModel(const std::string& path = ""); // path opcional para el stub de prueba
        bool initializeDevice();

    private:
        std::unique_ptr<IJTAGAdapter> adapter;
        std::unique_ptr<BoundaryScanEngine> engine;
        std::unique_ptr<DeviceModel> deviceModel;

        uint32_t detectedIDCODE;
        bool initialized;
    };

} // namespace JTAG