#include "ScanController.h"
#include "../parser/BSDLParser.h"
#include "../core/BoundaryScanEngine.h"
#include <iostream>
#include <iomanip>
#include <algorithm>
#include <memory>
#include <QDebug>

namespace JTAG {

    // ============================================================================
    // CONSTRUCTOR / DESTRUCTOR
    // ============================================================================

    ScanController::ScanController()
        : QObject(nullptr)
        , adapter(nullptr)
        , engine(nullptr)
        , deviceModel(nullptr)
        , bsdlCatalog(std::make_unique<BSDLCatalog>())
        , detectedIDCODE(0)
        , initialized(false)
    {
        workerThread = new QThread(this);
        // std::cout << "ScanController created\n";
    }

    ScanController::~ScanController() {
        disconnectAdapter();
    }

    // ============================================================================
    // NUEVO: DETECCIÓN DE SONDAS
    // ============================================================================

    std::vector<AdapterDescriptor> ScanController::getDetectedAdapters() const {
        // Delega la llamada a la Factoría
        return AdapterFactory::detectAdapters();
    }

    // ============================================================================
    // GESTIÓN DE ADAPTADOR Y DISPOSITIVO
    // ============================================================================

    bool ScanController::connectAdapter(AdapterType type, uint32_t clockSpeed) {
        if (adapter) disconnectAdapter();

        try {
            adapter = AdapterFactory::create(type);

            if (!adapter) return false;

            if (!adapter->open()) {
                adapter.reset();
                return false;
            }

            adapter->setClockSpeed(clockSpeed);
            initialized = false;
            detectedIDCODE = 0;
            return true;

        }
        catch (...) {
            adapter.reset();
            return false;
        }
    }

    void ScanController::disconnectAdapter() {
        if (adapter) {
            adapter->close();
            adapter.reset();
        }
        engine.reset();
        deviceModel.reset();
        initialized = false;
        detectedIDCODE = 0;
    }

    bool ScanController::isConnected() const {
        return adapter && adapter->isConnected();
    }

    std::string ScanController::getAdapterInfo() const {
        return adapter ? adapter->getInfo() : "";
    }

    uint32_t ScanController::detectDevice() {
        if (!adapter) return 0;

        // Engine temporal solo para IDCODE
        auto tempEngine = std::make_unique<BoundaryScanEngine>(adapter.get(), 0);
        detectedIDCODE = tempEngine->readIDCODE();

        if (detectedIDCODE == 0 || detectedIDCODE == 0xFFFFFFFF) {
            detectedIDCODE = 0;
            return 0;
        }
        return detectedIDCODE;
    }

    bool ScanController::loadBSDL(const std::string& bsdlPath) {
        BSDLParser parser;
        if (!parser.parse(bsdlPath)) {
            return false;
        }

        deviceModel = std::make_unique<DeviceModel>();
        deviceModel->loadFromData(parser.getData());

        // Recrear engine con tamaño BSR correcto
        if (adapter) {
            engine = std::make_unique<BoundaryScanEngine>(adapter.get(), deviceModel->getBSRLength());
        }

        return true;
    }

    std::string ScanController::getDeviceName() const {
        return deviceModel ? deviceModel->getDeviceName() : "";
    }

    std::string ScanController::getPackageInfo() const {
        return deviceModel ? deviceModel->getPackageInfo() : "";
    }

    // ============================================================================
    // INICIALIZACIÓN Y CONTROL
    // ============================================================================

    bool ScanController::initialize() {
        if (!adapter || !deviceModel || !engine) return false;

        if (!engine->reset()) return false;

        engine->loadInstruction(deviceModel->getInstruction("SAMPLE"));
        engine->samplePins();

        engine->loadInstruction(deviceModel->getInstruction("EXTEST"));

        // Hacer un sample adicional para tener valores iniciales en el BSR
        engine->samplePins();

        // Crear worker y moverlo al thread
        scanWorker = new ScanWorker(engine.get());
        scanWorker->moveToThread(workerThread);

        // Conectar señales
        connect(workerThread, &QThread::started, scanWorker, &ScanWorker::run);
        connect(scanWorker, &ScanWorker::pinsUpdated,
                this, &ScanController::onPinsUpdated);
        connect(scanWorker, &ScanWorker::errorOccurred,
                this, &ScanController::onWorkerError);

        initialized = true;
        return true;
    }

    bool ScanController::reset() {
        if (!engine) return false;
        initialized = false;
        return engine->reset();
    }

    // ============================================================================
    // CONTROL DE PINES
    // ============================================================================

    bool ScanController::setPin(const std::string& pinName, PinLevel level) {
        if (!deviceModel || !engine) return false;

        auto pinInfo = deviceModel->getPinInfo(pinName);
        if (!pinInfo) return false;

        if (pinInfo->outputCell != -1) {
            return engine->setPin(pinInfo->outputCell, level);
        }
        return false;
    }

    std::optional<PinLevel> ScanController::getPin(const std::string& pinName) const {
        if (!deviceModel || !engine) {
            qDebug() << "[ScanController::getPin] ERROR: deviceModel or engine is NULL";
            return std::nullopt;
        }

        auto pinInfo = deviceModel->getPinInfo(pinName);
        if (!pinInfo) {
            qDebug() << "[ScanController::getPin] ERROR: No pinInfo for" << QString::fromStdString(pinName);
            return std::nullopt;
        }

        // DEBUG: Primera vez, mostrar info de la celda
        static bool firstTime = true;
        if (firstTime && pinName == "IO_LED0") {
            qDebug() << "[ScanController::getPin] Pin IO_LED0: inputCell=" << pinInfo->inputCell
                     << ", outputCell=" << pinInfo->outputCell;
            firstTime = false;
        }

        if (pinInfo->inputCell != -1) {
            auto level = engine->getPin(pinInfo->inputCell);
            return level;
        }

        qDebug() << "[ScanController::getPin] WARNING: Pin" << QString::fromStdString(pinName)
                 << "has inputCell = -1";
        return std::nullopt;
    }

    std::vector<std::string> ScanController::getPinList() const {
        return deviceModel ? deviceModel->getPinNames() : std::vector<std::string>{};
    }

    bool ScanController::applyChanges() {
        return (engine && initialized) ? engine->applyChanges() : false;
    }

    bool ScanController::samplePins() {
        return (engine && initialized) ? engine->samplePins() : false;
    }

    bool ScanController::setPins(const std::map<std::string, PinLevel>& pins) {
        bool ok = true;
        for (auto const& [name, level] : pins) ok &= setPin(name, level);
        return ok;
    }

    std::map<std::string, PinLevel> ScanController::getPins(const std::vector<std::string>& pinNames) const {
        std::map<std::string, PinLevel> result;
        for (const auto& name : pinNames) {
            auto val = getPin(name);
            if (val) result[name] = *val;
        }
        return result;
    }

    bool ScanController::runTest(size_t numCycles) {
        return engine ? engine->runTestCycles(numCycles) : false;
    }

    bool ScanController::enterSAMPLE() {
        if (!initialize()) return false; // Asegura que BSDL esté cargado
        // SAMPLE/PRELOAD suele ser la instrucción segura por defecto
        uint32_t opcode = deviceModel->getInstruction("SAMPLE");
        if (opcode == 0xFFFFFFFF) opcode = deviceModel->getInstruction("SAMPLE/PRELOAD");

        if (!engine->loadInstruction(opcode, deviceModel->getIRLength())) return false;

        // Ejecutar un ciclo de sampleo inicial
        return engine->samplePins();
    }

    bool ScanController::enterEXTEST() {
        if (!engine) return false;
        // 1. Primero hacemos SAMPLE para cargar valores seguros en el BSR
        engine->samplePins();

        // 2. Cargamos EXTEST
        uint32_t opcode = deviceModel->getInstruction("EXTEST");
        return engine->loadInstruction(opcode, deviceModel->getIRLength());
    }

    bool ScanController::enterBYPASS() {
        if (!engine) return false;
        uint32_t opcode = deviceModel->getInstruction("BYPASS");
        return engine->loadInstruction(opcode, deviceModel->getIRLength());
    }

    bool ScanController::writeBus(const std::vector<std::string>& pinNames, uint32_t value) {
        if (!engine) return false;

        // Desglosar el valor entero a bits para cada pin
        for (size_t i = 0; i < pinNames.size(); i++) {
            // Bit 0 es el último pin de la lista (LSB), o el primero según tu convención.
            // Asumiremos que pinNames[0] es LSB.
            bool bitVal = (value >> i) & 1;

            // setPin en el engine solo actualiza memoria
            setPin(pinNames[i], bitVal ? PinLevel::HIGH : PinLevel::LOW);
        }

        // Aplicar todos los cambios en una sola transacción JTAG
        return applyChanges();
    }

    bool ScanController::loadDeviceModel(const std::string& path) {
        // Si path está vacío, cargamos un BSDL por defecto o "Stub" para probar la GUI
        if (path.empty()) {
            // Cargar un archivo hardcodeado para pruebas rápidas
            return loadBSDL("ejemplo.bsd");
        }
        return loadBSDL(path);
    }

    bool ScanController::initializeDevice() {
        return initialize();
    }

    bool ScanController::initializeBSDLCatalog(const std::string& directory) {
        if (!bsdlCatalog) {
            bsdlCatalog = std::make_unique<BSDLCatalog>();
        }
        return bsdlCatalog->scanDirectory(directory);
    }

    bool ScanController::autoLoadBSDL() {
        std::cout << "[ScanController::autoLoadBSDL] Called" << std::endl;

        if (!bsdlCatalog) {
            std::cout << "[ScanController::autoLoadBSDL] ERROR: bsdlCatalog is NULL!" << std::endl;
            return false;
        }

        if (detectedIDCODE == 0) {
            std::cout << "[ScanController::autoLoadBSDL] ERROR: detectedIDCODE is 0!" << std::endl;
            return false;
        }

        std::cout << "[ScanController::autoLoadBSDL] Looking for IDCODE 0x"
                  << std::hex << std::setfill('0') << std::setw(8) << detectedIDCODE
                  << std::dec << std::endl;
        std::cout << "[ScanController::autoLoadBSDL] Catalog has "
                  << bsdlCatalog->size() << " entries" << std::endl;

        auto bsdlPath = bsdlCatalog->findByIDCODE(detectedIDCODE);

        if (bsdlPath.has_value()) {
            std::cout << "[ScanController::autoLoadBSDL] FOUND! Path: "
                      << bsdlPath.value() << std::endl;
            bool loaded = loadBSDL(bsdlPath.value());
            std::cout << "[ScanController::autoLoadBSDL] loadBSDL returned: "
                      << (loaded ? "TRUE" : "FALSE") << std::endl;
            return loaded;
        }

        std::cout << "[ScanController::autoLoadBSDL] NOT FOUND in catalog!" << std::endl;
        return false;
    }

    std::string ScanController::getPinPort(const std::string& pinName) const {
        return deviceModel ? deviceModel->getPinPort(pinName) : "";
    }

    std::string ScanController::getPinType(const std::string& pinName) const {
        return deviceModel ? deviceModel->getPinType(pinName) : "";
    }

    std::string ScanController::getPinNumber(const std::string& pinName) const {
        return deviceModel ? deviceModel->getPinNumber(pinName) : "";
    }

    // ============================================================================
    // THREADING CONTROL
    // ============================================================================

    void ScanController::startPolling() {
        qDebug() << "[ScanController::startPolling] Called";
        if (scanWorker && !workerThread->isRunning()) {
            qDebug() << "[ScanController::startPolling] Starting worker thread";
            scanWorker->start();
            workerThread->start();
        } else {
            qDebug() << "[ScanController::startPolling] SKIPPED - worker:" << (scanWorker != nullptr)
                     << "running:" << (workerThread ? workerThread->isRunning() : false);
        }
    }

    void ScanController::stopPolling() {
        if (scanWorker) {
            scanWorker->stop();
            workerThread->quit();
            workerThread->wait();
        }
    }

    void ScanController::setPollInterval(int ms) {
        pollIntervalMs = ms;
        if (scanWorker) {
            scanWorker->setPollInterval(ms);
        }
    }

    void ScanController::setPinAsync(const std::string& pinName, PinLevel level) {
        if (!deviceModel || !scanWorker) return;

        auto pinInfo = deviceModel->getPinInfo(pinName);
        if (pinInfo && pinInfo->outputCell >= 0) {
            scanWorker->markDirtyPin(pinInfo->outputCell, level);
        }
    }

    // Slot para recibir datos del worker
    void ScanController::onPinsUpdated(std::vector<PinLevel> pins) {
        qDebug() << "[ScanController::onPinsUpdated] Received" << pins.size() << "pins from worker";
        // Actualizar cache local si es necesario
        // Reemitir señal para la GUI (esto hace que MainWindow la reciba)
        emit pinsDataReady(pins);
        qDebug() << "[ScanController::onPinsUpdated] Signal pinsDataReady emitted";
    }

    void ScanController::onWorkerError(QString message) {
        qWarning() << "Worker error:" << message;
        // Reemitir error para la GUI
        emit errorOccurred(message);
    }

} // namespace JTAG