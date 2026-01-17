#include "ScanWorker.h"
#include <QThread>
#include "utils/Log.h"

namespace JTAG {

    ScanWorker::ScanWorker(BoundaryScanEngine* engine, DeviceModel* model, QObject* parent)
        : QObject(parent)
        , engine(engine)
        , deviceModel(model)
    {
    }

    ScanWorker::~ScanWorker() {
        stop();
    }

    void ScanWorker::start() {
        running = true;
        emit started();
    }

    void ScanWorker::stop() {
        running = false;
        emit stopped();
    }

    void ScanWorker::setPollInterval(int ms) {
        pollIntervalMs = ms;
    }

    void ScanWorker::setSamplesPerSecond(int samplesPerSec) {
        if (samplesPerSec <= 0) samplesPerSec = 1;
        if (samplesPerSec > 1000) samplesPerSec = 1000;  // Límite crítico
        pollIntervalMs = 1000 / samplesPerSec;
    }

    void ScanWorker::forceReloadInstruction() {
        forceReload = true;
        LOG_INFO("[ScanWorker] Force reload instruction requested");
    }

    void ScanWorker::setScanMode(ScanMode mode) {
        currentMode = mode;

        // Sincronizar modo con el engine
        if (engine) {
            BoundaryScanEngine::OperationMode engineMode;
            switch (mode) {
                case ScanMode::SAMPLE:
                    engineMode = BoundaryScanEngine::OperationMode::SAMPLE;
                    break;
                case ScanMode::SAMPLE_SINGLE_SHOT:
                    engineMode = BoundaryScanEngine::OperationMode::SAMPLE;
                    // EXCEPCIÓN: Single-shot auto-inicia porque es una operación única
                    if (!running) {
                        start();
                    }
                    break;
                case ScanMode::EXTEST:
                    engineMode = BoundaryScanEngine::OperationMode::EXTEST;
                    break;
                case ScanMode::INTEST:
                    engineMode = BoundaryScanEngine::OperationMode::INTEST;
                    break;
                case ScanMode::BYPASS:
                    engineMode = BoundaryScanEngine::OperationMode::BYPASS;
                    break;
                default:
                    engineMode = BoundaryScanEngine::OperationMode::SAMPLE;
            }
            engine->setOperationMode(engineMode);
        }
    }

    void ScanWorker::markDirtyPin(size_t cellIndex, PinLevel level) {
        std::lock_guard<std::mutex> lock(dirtyMutex);
        dirtyPins.push({cellIndex, level});  // Agregar a cola FIFO
    }

    bool ScanWorker::hasDirtyPins() const {
        std::lock_guard<std::mutex> lock(dirtyMutex);
        return !dirtyPins.empty();
    }

    // --------------------------------------------------------------------------
    // LÓGICA PRINCIPAL DEL HILO
    // --------------------------------------------------------------------------
    void ScanWorker::run() {
        LOG_INFO("[ScanWorker] Thread started");

        ScanMode lastMode = ScanMode::SAMPLE;
        bool firstRun = true;

        // Vector para almacenar pines (se redimensiona dinámicamente con protección)
        std::vector<PinLevel> pins;
        size_t lastKnownBsrLength = 0;

        while (running) {
            try {
                // Verificar que tenemos modelo válido
                if (!deviceModel || !engine) {
                    QThread::msleep(50);
                    continue;
                }

                // ===== SANITY CHECK: Verificar BSR length válido =====
                size_t currentBsrLength = engine->getBSRLength();

                // Protección contra valores basura (límite razonable: 10000 bits)
                if (currentBsrLength == 0 || currentBsrLength > 10000) {
                    QThread::msleep(50);
                    continue;  // Modelo no inicializado o valor inválido
                }

                // Redimensionar vector si cambió el tamaño del BSR
                if (currentBsrLength != lastKnownBsrLength) {
                    try {
                        pins.clear();
                        pins.reserve(currentBsrLength);
                        lastKnownBsrLength = currentBsrLength;
                        LOG_DEBUG("[ScanWorker] BSR buffer resized to" << currentBsrLength);
                    } catch (const std::exception& e) {
                        LOG_ERROR("[ScanWorker] Reserving memory:" << e.what());
                        QThread::msleep(100);
                        continue;
                    }
                }
                // =====================================================

                ScanMode targetMode = currentMode.load();

                // 1. CARGA DE INSTRUCCIÓN (Solo cuando cambia el modo)
                // Optimización: Solo cargamos instrucción cuando:
                // - Es la primera ejecución
                // - El modo cambió (SAMPLE -> EXTEST, etc.)
                // - Se solicitó recarga forzada (después de JTAG reset)
                bool forceReloadRequested = forceReload.exchange(false); // Leer y resetear flag
                bool modeChanged = (targetMode != lastMode) || firstRun || forceReloadRequested;

                if (modeChanged) {

                    if (targetMode == ScanMode::EXTEST || targetMode == ScanMode::INTEST) {

                        // 1. Tomar la "última foto" de la realidad
                        engine->samplePins();

                        // 2. 
                        // Actualizamos el buffer de escritura con esa foto reciente.
                        // Ahora Escritura == Lectura (Realidad).
                        engine->syncWriteBufferFromRead();

                        // 3. (Opcional pero recomendado) Preload para asegurar que el chip lo tenga listo
                        engine->preloadBSR();
                    }

                    std::string instrName = "SAMPLE"; // Default
                    if (targetMode == ScanMode::SAMPLE_SINGLE_SHOT) instrName = "SAMPLE";
                    if (targetMode == ScanMode::EXTEST) instrName = "EXTEST";
                    if (targetMode == ScanMode::INTEST) instrName = "INTEST";
                    if (targetMode == ScanMode::BYPASS) instrName = "BYPASS";

                    uint32_t opcode = deviceModel->getInstruction(instrName);

                    // Fallback para SAMPLE
                    if (opcode == 0xFFFFFFFF && targetMode == ScanMode::SAMPLE)
                        opcode = deviceModel->getInstruction("SAMPLE/PRELOAD");

                    size_t irLen = deviceModel->getIRLength();

                    // Cargar la instrucción
                    if (!engine->loadInstruction(opcode, irLen)) {
                        LOG_WARNING("[ScanWorker] Failed to load instruction:" << QString::fromStdString(instrName));
                    } else {
                        LOG_INFO("[ScanWorker] Loaded instruction:" << QString::fromStdString(instrName));

                        // IEEE 1149.1: En EXTEST/INTEST, ejecutar UPDATE-DR inicial
                        // Esto "congela" el chip inmediatamente con el estado actual del BSR
                        if (targetMode == ScanMode::EXTEST || targetMode == ScanMode::INTEST) {
                            if (!engine->applyChanges()) {
                                LOG_WARNING("[ScanWorker] Failed to apply initial UPDATE-DR for" << QString::fromStdString(instrName));
                            } else {
                                LOG_DEBUG("[ScanWorker] Applied initial UPDATE-DR -" << QString::fromStdString(instrName) << "now active");
                            }
                        }
                    }

                    lastMode = targetMode;
                    firstRun = false;
                }

                // 2. EJECUCIÓN DEL MODO
                if (targetMode == ScanMode::EXTEST || targetMode == ScanMode::INTEST) {
                    if (hasDirtyPins()) {
                        processDirtyPins();
                        if (!engine->applyChanges()) {
                            QString modeStr = (targetMode == ScanMode::EXTEST) ? "EXTEST" : "INTEST";
                            emit errorOccurred(QString("Failed to apply changes in %1").arg(modeStr));
                        }
                    }
                } else if (targetMode == ScanMode::SAMPLE || targetMode == ScanMode::SAMPLE_SINGLE_SHOT) {
                    engine->samplePins();
                } else if (targetMode == ScanMode::BYPASS) {
                    // Bypass: no operaciones BSR
                }

                // 3. ACTUALIZAR GUI
                if (targetMode != ScanMode::BYPASS && lastKnownBsrLength > 0) {
                    pins.resize(lastKnownBsrLength);

                    for (size_t i = 0; i < lastKnownBsrLength; ++i) {
                        auto level = (targetMode == ScanMode::EXTEST || targetMode == ScanMode::INTEST)
                            ? engine->getPin(i)
                            : engine->getPinReadback(i);

                        pins[i] = level.value_or(PinLevel::HIGH_Z);
                    }

                    // Emitir copia, no mover (para mantener el vector reutilizable)
                    auto pinsPtr = std::make_shared<const std::vector<PinLevel>>(pins);
                    emit pinsUpdated(pinsPtr);
                }

                // Single-shot auto-stop
                if (targetMode == ScanMode::SAMPLE_SINGLE_SHOT) {
                    LOG_INFO("[ScanWorker] Single-shot capture complete, stopping");
                    running = false;
                    emit stopped();
                }

            }
            catch (const std::exception& e) {
                emit errorOccurred(QString("Worker exception: %1").arg(e.what()));
            }

            QThread::msleep(pollIntervalMs);
        }

        LOG_INFO("[ScanWorker] Thread stopped");
    } // <--- ESTA LLAVE CIERRA LA FUNCIÓN RUN()

    // --------------------------------------------------------------------------
    // FUNCIONES AUXILIARES (FUERA DE RUN)
    // --------------------------------------------------------------------------

    void ScanWorker::processDirtyPins() {
        std::lock_guard<std::mutex> lock(dirtyMutex);

        LOG_VERBOSE("[ScanWorker::processDirtyPins] Queue size:" << dirtyPins.size());

        // Procesar TODOS los cambios en orden FIFO
        while (!dirtyPins.empty()) {
            auto [cellIndex, level] = dirtyPins.front();

            LOG_VERBOSE("  Applying: Cell" << cellIndex << "→" << (level == JTAG::PinLevel::HIGH ? "HIGH" : (level == JTAG::PinLevel::LOW ? "LOW" : "HIGH_Z")));

            // setPin() modifica bsr (buffer TDI)
            // Este valor se mantiene automáticamente entre llamadas
            engine->setPin(cellIndex, level);
            dirtyPins.pop();
        }
    }

    // Función auxiliar (aunque ahora hacemos la carga en el bucle, la mantenemos por compatibilidad)
    void ScanWorker::applyMode(ScanMode mode) {
        // La lógica real está ahora integrada en run() para mayor robustez
        Q_UNUSED(mode);
    }

} // namespace JTAG