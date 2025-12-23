#include "ScanWorker.h"
#include <QThread>
#include <QDebug>

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

    void ScanWorker::forceReloadInstruction() {
        forceReload = true;
        qDebug() << "[ScanWorker] Force reload instruction requested";
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
        dirtyPins[cellIndex] = level;
    }

    bool ScanWorker::hasDirtyPins() const {
        std::lock_guard<std::mutex> lock(dirtyMutex);
        return !dirtyPins.empty();
    }

    // --------------------------------------------------------------------------
    // LÓGICA PRINCIPAL DEL HILO
    // --------------------------------------------------------------------------
    void ScanWorker::run() {
        qDebug() << "[ScanWorker] Thread started";

        ScanMode lastMode = ScanMode::SAMPLE;
        bool firstRun = true;

        // ===== OPTIMIZACIÓN: Mover vector FUERA del loop =====
        // Evita malloc/free en cada iteración (hot path)
        // Con 200 pines @ 50Hz = 10,000 allocations/sec → 0 allocations
        std::vector<PinLevel> pins;
        if (deviceModel) {
            pins.reserve(deviceModel->getBSRLength());
        }
        // =====================================================

        while (running) {
            try {
                if (!deviceModel) {
                    QThread::msleep(50);
                    continue;
                }

                ScanMode targetMode = currentMode.load();

                // 1. CARGA DE INSTRUCCIÓN (Solo cuando cambia el modo)
                // Optimización: Solo cargamos instrucción cuando:
                // - Es la primera ejecución
                // - El modo cambió (SAMPLE -> EXTEST, etc.)
                // - Se solicitó recarga forzada (después de JTAG reset)
                bool forceReloadRequested = forceReload.exchange(false); // Leer y resetear flag
                bool modeChanged = (targetMode != lastMode) || firstRun || forceReloadRequested;

                if (modeChanged) {
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
                        qDebug() << "[ScanWorker] Failed to load instruction:" << QString::fromStdString(instrName);
                    } else {
                        qDebug() << "[ScanWorker] Loaded instruction:" << QString::fromStdString(instrName);
                    }

                    lastMode = targetMode;
                    firstRun = false;
                }

                // 2. EJECUCIÓN DEL MODO
                if (targetMode == ScanMode::EXTEST || targetMode == ScanMode::INTEST) {
                    // Ambos modos EXTEST e INTEST usan el mismo mecanismo BSR
                    // Solo difiere la instrucción cargada (EXTEST vs INTEST)
                    // EXTEST: controla pines externos
                    // INTEST: prueba lógica interna

                    // A) Procesar cambios nuevos de la GUI
                    if (hasDirtyPins()) {
                        processDirtyPins();

                        // B) Aplicar cambios INMEDIATAMENTE
                        // Como BoundaryScanEngine ahora mantiene bsr separado,
                        // no necesitamos restaurar manualmente los valores
                        if (!engine->applyChanges()) {
                            QString modeStr = (targetMode == ScanMode::EXTEST) ? "EXTEST" : "INTEST";
                            emit errorOccurred(QString("Failed to apply changes in %1").arg(modeStr));
                        }
                    }
                    // Si no hay cambios, NO hacer applyChanges innecesario
                    // (optimización para reducir tráfico JTAG)
                }
                else if (targetMode == ScanMode::SAMPLE || targetMode == ScanMode::SAMPLE_SINGLE_SHOT) {
                    // Modo SAMPLE (Solo lectura) - continuo o single-shot
                    engine->samplePins();
                }
                else if (targetMode == ScanMode::BYPASS) {
                    // Modo BYPASS: instrucción ya cargada, no hacer operaciones BSR
                    // El chip está en bypass, el BSR no es accesible
                    // Modo estático: no polling necesario
                }

                // 3. ACTUALIZAR GUI
                size_t bsrLen = engine->getBSRLength();

                // ===== OPTIMIZACIÓN: resize() en lugar de declarar nuevo vector =====
                // Mantiene capacidad pre-alocada, solo ajusta tamaño lógico
                pins.resize(bsrLen);
                // ===================================================================

                if (targetMode != ScanMode::BYPASS) {
                    // MODE-AWARE: Lectura según el modo activo
                    // EXTEST/INTEST: Usuario edita → leer buffer de escritura (bsr estable)
                    // SAMPLE: Solo lectura → leer buffer capturado (bsrCapture actualizado)
                    for (size_t i = 0; i < bsrLen; ++i) {
                        auto level = (targetMode == ScanMode::EXTEST || targetMode == ScanMode::INTEST)
                            ? engine->getPin(i)          // EXTEST/INTEST: lee bsr (ediciones del usuario)
                            : engine->getPinReadback(i); // SAMPLE: lee bsrCapture (estado real del chip)

                        // ===== OPTIMIZACIÓN: Asignación directa (no push_back) =====
                        pins[i] = level.value_or(PinLevel::HIGH_Z);
                        // ===========================================================
                    }
                } else {
                    // En modo BYPASS, el BSR no es accesible
                    // Enviar estados high-Z a la GUI
                    for (size_t i = 0; i < bsrLen; ++i) {
                        pins[i] = PinLevel::HIGH_Z;  // Asignación directa
                    }
                }

                // FASE 2: Usar std::make_shared para asignación eficiente
                // make_shared asigna el bloque de control y el objeto en UNA SOLA llamada al heap
                // Evita 3 copias profundas (Qt::QueuedConnection solo incrementa refcount)
                auto pinsPtr = std::make_shared<const std::vector<PinLevel>>(std::move(pins));
                emit pinsUpdated(pinsPtr);

                // Si estamos en modo single-shot, detener automáticamente después de la captura
                if (targetMode == ScanMode::SAMPLE_SINGLE_SHOT) {
                    qDebug() << "[ScanWorker] Single-shot capture complete, stopping";
                    running = false;
                    emit stopped();
                }

            }
            catch (const std::exception& e) {
                emit errorOccurred(QString("Worker exception: %1").arg(e.what()));
            }

            QThread::msleep(pollIntervalMs);
        }

        qDebug() << "[ScanWorker] Thread stopped";
    } // <--- ESTA LLAVE CIERRA LA FUNCIÓN RUN()

    // --------------------------------------------------------------------------
    // FUNCIONES AUXILIARES (FUERA DE RUN)
    // --------------------------------------------------------------------------

    void ScanWorker::processDirtyPins() {
        std::lock_guard<std::mutex> lock(dirtyMutex);
        for (const auto& [cellIndex, level] : dirtyPins) {
            // setPin() modifica bsr (buffer TDI)
            // Este valor se mantiene automáticamente entre llamadas
            engine->setPin(cellIndex, level);
        }
        dirtyPins.clear();
    }

    // Función auxiliar (aunque ahora hacemos la carga en el bucle, la mantenemos por compatibilidad)
    void ScanWorker::applyMode(ScanMode mode) {
        // La lógica real está ahora integrada en run() para mayor robustez
        Q_UNUSED(mode);
    }

} // namespace JTAG