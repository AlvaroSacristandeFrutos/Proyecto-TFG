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
        , detectedIDCODE(0)
        , initialized(false)
    {
        workerThread = new QThread(this);
        std::cout << "[ScanController] Constructor: ScanController created\n";
    }

    ScanController::~ScanController() {
        disconnectAdapter();
    }

    // ============================================================================
    // NUEVO: DETECCIÓN DE SONDAS
    // ============================================================================

    std::vector<AdapterDescriptor> ScanController::getDetectedAdapters() const {
        // Delega la llamada a la Factoría (lista estática)
        return AdapterFactory::getAvailableAdapters();
    }

    // ============================================================================
    // GESTIÓN DE ADAPTADOR Y DISPOSITIVO
    // ============================================================================

    bool ScanController::connectAdapter(AdapterType type, uint32_t clockSpeed) {
        std::cout << "[ScanController] connectAdapter: type=" << static_cast<int>(type)
                  << " clockSpeed=" << clockSpeed << "\n";

        if (adapter) {
            std::cout << "[ScanController] Disconnecting existing adapter\n";
            disconnectAdapter();
        }

        try {
            adapter = AdapterFactory::create(type);

            if (!adapter) {
                std::cerr << "[ScanController] ERROR: Failed to create adapter\n";
                return false;
            }

            std::cout << "[ScanController] Opening adapter...\n";
            if (!adapter->open()) {
                std::cerr << "[ScanController] ERROR: Failed to open adapter\n";
                adapter.reset();
                return false;
            }

            std::cout << "[ScanController] Setting clock speed to " << clockSpeed << " Hz\n";
            adapter->setClockSpeed(clockSpeed);
            initialized = false;
            detectedIDCODE = 0;

            // NUEVO: Si es MockAdapter, auto-generar DeviceModel
            if (type == AdapterType::MOCK) {
                createMockDeviceModel();
                qDebug() << "[ScanController] MockAdapter connected - auto-generated DeviceModel";
            }

            return true;

        }
        catch (...) {
            adapter.reset();
            return false;
        }
    }

    bool ScanController::connectAdapter(const AdapterDescriptor& descriptor, uint32_t clockSpeed) {
        if (adapter) disconnectAdapter();

        try {
            // Use factory with deviceID to create specific adapter instance
            adapter = AdapterFactory::create(descriptor.type, descriptor.deviceID);

            if (!adapter) return false;

            if (!adapter->open()) {
                adapter.reset();
                return false;
            }

            adapter->setClockSpeed(clockSpeed);
            initialized = false;
            detectedIDCODE = 0;

            // NUEVO: Si es MockAdapter, auto-generar DeviceModel
            if (descriptor.type == AdapterType::MOCK) {
                createMockDeviceModel();
                qDebug() << "[ScanController] MockAdapter connected - auto-generated DeviceModel";
            }

            std::cout << "[ScanController] Connected to: " << descriptor.name
                      << " (" << descriptor.serialNumber << ")\n";

            return true;

        }
        catch (const std::exception& e) {
            std::cerr << "[ScanController] Exception connecting adapter: " << e.what() << "\n";
            adapter.reset();
            return false;
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

    void ScanController::unloadBSDL() {
        // Detener polling si está activo
        stopPolling();

        // Limpiar el modelo del dispositivo, engine e IDCODE del target
        // Mantener SOLO el adaptador (sonda) conectado
        engine.reset();
        deviceModel.reset();
        initialized = false;
        detectedIDCODE = 0;  // Limpiar IDCODE del target

        // NO tocar: adapter (la sonda sigue conectada)
        std::cout << "[ScanController] BSDL unloaded - adapter still connected\n";
    }

    bool ScanController::isConnected() const {
        return adapter && adapter->isConnected();
    }

    std::string ScanController::getAdapterInfo() const {
        return adapter ? adapter->getInfo() : "";
    }

    uint32_t ScanController::detectDevice() {
        std::cout << "[ScanController] detectDevice: Reading IDCODE...\n";

        if (!adapter) {
            std::cerr << "[ScanController] ERROR: No adapter connected\n";
            return 0;
        }

        // Engine temporal solo para IDCODE
        auto tempEngine = std::make_unique<BoundaryScanEngine>(adapter.get(), 0);
        detectedIDCODE = tempEngine->readIDCODE();

        std::cout << "[ScanController] IDCODE read: 0x" << std::hex << std::setw(8) << std::setfill('0')
                  << detectedIDCODE << std::dec << "\n";

        if (detectedIDCODE == 0 || detectedIDCODE == 0xFFFFFFFF) {
            std::cerr << "[ScanController] WARNING: Invalid IDCODE (0x00000000 or 0xFFFFFFFF)\n";
            detectedIDCODE = 0;
            return 0;
        }
        return detectedIDCODE;
    }

    bool ScanController::loadBSDL(const std::filesystem::path& bsdlPath) {
        std::cout << "[ScanController] loadBSDL: Loading file: " << bsdlPath.string() << "\n";

        BSDLParser parser;
        if (!parser.parse(bsdlPath)) {
            std::cerr << "[ScanController] ERROR: Failed to parse BSDL file\n";
            return false;
        }

        deviceModel = std::make_unique<DeviceModel>();
        deviceModel->loadFromData(parser.getData());

        std::cout << "[ScanController] Device: " << deviceModel->getDeviceName()
                  << " BSR Length: " << deviceModel->getBSRLength() << " bits\n";

        // Recrear engine con tamaño BSR correcto
        if (adapter) {
            engine = std::make_unique<BoundaryScanEngine>(adapter.get(), deviceModel->getBSRLength());
            std::cout << "[ScanController] BoundaryScanEngine recreated with BSR length: "
                      << deviceModel->getBSRLength() << "\n";
        }

        std::cout << "[ScanController] BSDL loaded successfully\n";
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
        std::cout << "[ScanController] initialize: Starting device initialization...\n";

        if (!adapter || !deviceModel || !engine) {
            std::cerr << "[ScanController] ERROR: Missing components - adapter:" << (adapter ? "OK" : "NULL")
                      << " deviceModel:" << (deviceModel ? "OK" : "NULL")
                      << " engine:" << (engine ? "OK" : "NULL") << "\n";
            return false;
        }

        // Reset TAP a estado conocido
        std::cout << "[ScanController] Resetting TAP controller...\n";
        if (!engine->reset()) {
            std::cerr << "[ScanController] ERROR: Failed to reset TAP\n";
            return false;
        }

        // ========== SECUENCIA IEEE 1149.1 (Solución A) ==========

        // Paso 1: Cargar instrucción SAMPLE/PRELOAD
        std::cout << "[ScanController] Loading SAMPLE/PRELOAD instruction...\n";
        uint32_t sampleInstr = deviceModel->getInstruction("SAMPLE/PRELOAD");
        if (sampleInstr == 0xFFFFFFFF) {
            // Fallback si no existe SAMPLE/PRELOAD
            std::cout << "[ScanController] SAMPLE/PRELOAD not found, trying SAMPLE...\n";
            sampleInstr = deviceModel->getInstruction("SAMPLE");
        }

        std::cout << "[ScanController] SAMPLE instruction opcode: 0x" << std::hex << sampleInstr << std::dec << "\n";

        if (!engine->loadInstruction(sampleInstr, deviceModel->getIRLength())) {
            std::cerr << "[ScanController] ERROR: Failed to load SAMPLE instruction\n";
            return false;
        }

        // Paso 2: Sample para capturar estado actual seguro
        if (!engine->samplePins()) {
            std::cerr << "[ScanController] Failed to sample pins\n";
            return false;
        }

        // Paso 3: Precargar esos valores en el registro de actualización
        // (sin cambiar pines físicos, porque estamos en SAMPLE/PRELOAD)
        if (!engine->preloadBSR()) {
            std::cerr << "[ScanController] Failed to preload BSR\n";
            return false;
        }

        // Paso 4: Cargar instrucción EXTEST
        // Los pines tomarán los valores precargados de forma segura
        uint32_t extestInstr = deviceModel->getInstruction("EXTEST");
        if (!engine->loadInstruction(extestInstr, deviceModel->getIRLength())) {
            std::cerr << "[ScanController] Failed to load EXTEST instruction\n";
            return false;
        }

        // NO ejecutar samplePins() o applyChanges() aquí
        // El worker lo hará en su primer ciclo

        // ========== FIN SECUENCIA IEEE 1149.1 ==========

        // Crear worker y moverlo al thread
        scanWorker = new ScanWorker(engine.get(), deviceModel.get());
        scanWorker->moveToThread(workerThread);

        // Conectar señales (especificar Qt::QueuedConnection explícitamente para cross-thread)
        connect(workerThread, &QThread::started, scanWorker, &ScanWorker::run);
        connect(scanWorker, &ScanWorker::pinsUpdated,
                this, &ScanController::onPinsUpdated, Qt::QueuedConnection);
        connect(scanWorker, &ScanWorker::errorOccurred,
                this, &ScanController::onWorkerError, Qt::QueuedConnection);
        connect(scanWorker, &ScanWorker::stopped,
                this, &ScanController::onWorkerStopped, Qt::QueuedConnection);

        initialized = true;
        return true;
    }

    bool ScanController::reset() {
        if (!engine) return false;
        initialized = false;
        return engine->reset();
    }

    bool ScanController::resetJTAGStateMachine() {
        if (!engine) return false;
        return engine->resetJTAGStateMachine();
    }

    // ============================================================================
    // CONTROL DE PINES
    // ============================================================================

    bool ScanController::setPin(const std::string& pinName, PinLevel level) {
        if (!deviceModel || !engine) return false;

        auto pinInfo = deviceModel->getPinInfo(pinName);
        if (!pinInfo) {
            qWarning() << "setPin: Pin not found" << QString::fromStdString(pinName);
            return false;
        }

        // PROTECCIÓN: Si outputCell es negativo, NO INTENTAR ESCRIBIR
        if (pinInfo->outputCell < 0) {
            // Es un input (como CLKIN), ignoramos la escritura silenciosamente o con aviso debug
            qDebug() << "[ScanController] Skipping write to input pin:" << QString::fromStdString(pinName);
            return false;
        }

        return engine->setPin(pinInfo->outputCell, level);
    }

    std::optional<PinLevel> ScanController::getPin(const std::string& pinName) const {
        if (!deviceModel || !engine) {
            return std::nullopt;
        }

        auto pinInfo = deviceModel->getPinInfo(pinName);
        if (!pinInfo) {
            return std::nullopt;
        }

        // Leer del buffer correcto según el tipo de celda:

        // 1. Si tiene celda INPUT, leer del buffer CAPTURADO (TDO)
        if (pinInfo->inputCell != -1) {
            auto level = engine->getPinReadback(pinInfo->inputCell);
            return level;
        }

        // 2. Si solo tiene celda OUTPUT, leer del buffer DESEADO (TDI)
        //    (para mostrar al usuario qué valor estamos enviando)
        if (pinInfo->outputCell != -1) {
            auto level = engine->getPin(pinInfo->outputCell);
            return level;
        }

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

        // Setear modo antes de samplePins
        engine->setOperationMode(BoundaryScanEngine::OperationMode::SAMPLE);

        // Ejecutar un ciclo de sampleo inicial
        return engine->samplePins();
    }

    bool ScanController::enterEXTEST() {
        if (!engine || !deviceModel) return false;

        // ========== SECUENCIA IEEE 1149.1 (Solución A) ==========

        // Paso 1: Cargar SAMPLE/PRELOAD
        uint32_t sampleInstr = deviceModel->getInstruction("SAMPLE/PRELOAD");
        if (sampleInstr == 0xFFFFFFFF) {
            sampleInstr = deviceModel->getInstruction("SAMPLE");
        }

        if (!engine->loadInstruction(sampleInstr, deviceModel->getIRLength())) {
            return false;
        }

        // Paso 2: Sample para capturar estado actual
        if (!engine->samplePins()) {
            return false;
        }

        // Paso 3: Precargar valores seguros
        if (!engine->preloadBSR()) {
            return false;
        }

        // Paso 4: Cargar EXTEST (sin scanDR después)
        uint32_t extestInstr = deviceModel->getInstruction("EXTEST");
        if (!engine->loadInstruction(extestInstr, deviceModel->getIRLength())) {
            return false;
        }

        // ========== FIN SECUENCIA IEEE 1149.1 ==========

        // Setear modo al final
        engine->setOperationMode(BoundaryScanEngine::OperationMode::EXTEST);

        return true;
    }

    bool ScanController::enterBYPASS() {
        if (!engine) return false;
        engine->setOperationMode(BoundaryScanEngine::OperationMode::BYPASS);
        uint32_t opcode = deviceModel->getInstruction("BYPASS");
        return engine->loadInstruction(opcode, deviceModel->getIRLength());
    }

    bool ScanController::enterINTEST() {
        if (!engine || !deviceModel) return false;

        // Secuencia segura IEEE 1149.1 (igual que EXTEST)
        // INTEST prueba la lógica interna del chip, no los pines externos

        // Paso 1: Cargar SAMPLE/PRELOAD
        uint32_t sampleInstr = deviceModel->getInstruction("SAMPLE/PRELOAD");
        if (sampleInstr == 0xFFFFFFFF) {
            sampleInstr = deviceModel->getInstruction("SAMPLE");
        }

        if (!engine->loadInstruction(sampleInstr, deviceModel->getIRLength())) {
            std::cerr << "[ScanController] Failed to load SAMPLE for INTEST sequence\n";
            return false;
        }

        // Paso 2: Capturar estado actual
        if (!engine->samplePins()) {
            std::cerr << "[ScanController] Failed to sample pins for INTEST sequence\n";
            return false;
        }

        // Paso 3: Precargar valores seguros en el update latch
        if (!engine->preloadBSR()) {
            std::cerr << "[ScanController] Failed to preload BSR for INTEST sequence\n";
            return false;
        }

        // Paso 4: Cargar instrucción INTEST
        uint32_t intestInstr = deviceModel->getInstruction("INTEST");
        if (intestInstr == 0xFFFFFFFF) {
            std::cerr << "[ScanController] INTEST instruction not found in BSDL\n";
            return false;
        }

        if (!engine->loadInstruction(intestInstr, deviceModel->getIRLength())) {
            std::cerr << "[ScanController] Failed to load INTEST instruction\n";
            return false;
        }

        // Setear modo al final
        engine->setOperationMode(BoundaryScanEngine::OperationMode::INTEST);

        std::cout << "[ScanController] Successfully entered INTEST mode\n";
        return true;
    }

    void ScanController::setEngineOperationMode(BoundaryScanEngine::OperationMode mode) {
        if (engine) {
            engine->setOperationMode(mode);
        }
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

    void ScanController::forceReloadInstruction() {
        if (scanWorker) {
            scanWorker->forceReloadInstruction();
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

    void ScanController::onWorkerStopped() {
        qDebug() << "[ScanController::onWorkerStopped] Worker stopped (single-shot complete)";
        // Detener el thread cuando el worker se detiene (para modo single-shot)
        if (workerThread && workerThread->isRunning()) {
            workerThread->quit();
            workerThread->wait();
            qDebug() << "[ScanController::onWorkerStopped] Thread stopped";
        }
    }

    void ScanController::createMockDeviceModel() {
        qDebug() << "[ScanController] Creating mock DeviceModel for MockAdapter";

        // Crear BSDL data simulado para MockAdapter
        BSDLData mockData;
        mockData.entityName = "MOCK_DEVICE";
        mockData.idCode = 0x12345678;
        mockData.boundaryLength = 256;  // 256 bits de BSR
        mockData.instructionLength = 8;
        mockData.physicalPinMap = "BGA";

        // Agregar instrucciones básicas
        Instruction sampInstr;
        sampInstr.name = "SAMPLE";
        sampInstr.opcodes.push_back("00000001");
        mockData.instructions.push_back(sampInstr);

        Instruction extestInstr;
        extestInstr.name = "EXTEST";
        extestInstr.opcodes.push_back("00000000");
        mockData.instructions.push_back(extestInstr);

        // Crear 32 pines simulados (8 bits por pin = 256 bits totales)
        for (int i = 0; i < 32; i++) {
            // Celda INPUT para cada pin
            BoundaryCell inputCell;
            inputCell.cellNumber = i * 8;
            inputCell.portName = "MOCK_PIN_" + std::to_string(i);
            inputCell.function = CellFunction::INPUT;
            inputCell.safeValue = SafeBit::DONT_CARE;
            inputCell.controlCell = -1;
            inputCell.disableValue = SafeBit::DONT_CARE;
            mockData.boundaryCells.push_back(inputCell);

            // Agregar pin mapping (nombre de pin → número físico)
            mockData.pinMaps["MOCK_PIN_" + std::to_string(i)].push_back(std::to_string(i + 1));
        }

        // Crear DeviceModel y cargar datos simulados
        deviceModel = std::make_unique<JTAG::DeviceModel>();
        deviceModel->loadFromData(mockData);

        // Configurar IDCODE detectado
        detectedIDCODE = 0x12345678;

        qDebug() << "[ScanController] Created mock DeviceModel with"
                 << deviceModel->getAllPins().size() << "pins";
    }

    bool ScanController::isNoTargetDetected() const {
        if (!engine) return false;
        return engine->isNoTargetDetected();
    }
    void ScanController::setScanMode(ScanMode mode) {
        if (scanWorker) {
            scanWorker->setScanMode(mode);

            // Auto-iniciar el thread si el modo requiere polling y no está corriendo
            // BYPASS no necesita polling (modo estático)
            bool needsPolling = (mode != ScanMode::BYPASS);

            if (needsPolling && workerThread && !workerThread->isRunning()) {
                qDebug() << "[ScanController] Auto-starting thread for mode:" << static_cast<int>(mode);
                scanWorker->start();
                workerThread->start();
            }
        }
    }

} // namespace JTAG