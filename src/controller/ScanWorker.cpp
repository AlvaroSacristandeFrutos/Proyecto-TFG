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

    void ScanWorker::setScanMode(ScanMode mode) {
        currentMode = mode;
        // Forzamos que la siguiente iteración detecte el cambio
    }

    void ScanWorker::markDirtyPin(size_t cellIndex, PinLevel level) {
        std::lock_guard<std::mutex> lock(dirtyMutex);
        dirtyPins[cellIndex] = level;
    }

    bool ScanWorker::hasDirtyPins() const {
        std::lock_guard<std::mutex> lock(dirtyMutex);
        return !dirtyPins.empty();
    }

    void ScanWorker::run() {
        qDebug() << "[ScanWorker] Thread started";

        // Al iniciar, asumimos que el chip no tiene instrucción cargada
        lastAppliedMode = ScanMode::SAMPLE; // Valor inicial
        bool firstRun = true;

        while (running) {
            try {
                if (!deviceModel) {
                    QThread::msleep(100);
                    continue;
                }

                ScanMode targetMode = currentMode.load();

                // 1. GESTIÓN DE CAMBIO DE MODO O REFRESCO
                // Si cambiamos de modo, o si estamos en SAMPLE (para recargar por seguridad), cargamos la instrucción.
                // NOTA: En EXTEST/INTEST no recargamos la instrucción en cada ciclo para no "parpadear" los pines,
                // salvo que el modo haya cambiado.
                if (targetMode != lastAppliedMode || targetMode == ScanMode::SAMPLE || firstRun) {
                    applyMode(targetMode);
                    lastAppliedMode = targetMode;
                    firstRun = false;
                }

                // 2. LÓGICA DE ESCRITURA (Solo para EXTEST / INTEST)
                if (targetMode == ScanMode::EXTEST || targetMode == ScanMode::INTEST) {
                    // Si hay cambios pendientes, actualizamos el BSR en memoria
                    if (hasDirtyPins()) {
                        processDirtyPins();
                    }
                    // EN EXTEST SIEMPRE ESCRIBIMOS (applyChanges) para mantener los pines estables
                    // applyChanges() hace: Shift-DR con los datos de salida
                    if (!engine->applyChanges()) {
                        emit errorOccurred("Failed to apply changes in EXTEST");
                    }
                }
                // 3. LÓGICA DE LECTURA (SAMPLE)
                else if (targetMode == ScanMode::SAMPLE) {
                    // En SAMPLE solo leemos
                    if (!engine->samplePins()) {
                        emit errorOccurred("Failed to sample pins");
                    }
                }
                // 4. BYPASS
                else if (targetMode == ScanMode::BYPASS) {
                    // En Bypass podríamos hacer un test de integridad
                    // De momento no hacemos nada o leemos 1 bit dummy
                    // engine->runTestCycles(1);
                }

                // 5. REPORTAR DATOS A GUI
                // Incluso en EXTEST, applyChanges() nos devuelve lo que había en la entrada (sampled inputs)
                // Así que siempre podemos actualizar la GUI.
                if (targetMode != ScanMode::BYPASS) {
                    std::vector<PinLevel> pins;
                    size_t bsrLen = engine->getBSRLength();
                    pins.reserve(bsrLen);

                    for (size_t i = 0; i < bsrLen; ++i) {
                        auto level = engine->getPin(i);
                        pins.push_back(level.value_or(PinLevel::HIGH_Z));
                    }
                    emit pinsUpdated(pins);
                }

            }
            catch (const std::exception& e) {
                emit errorOccurred(QString("Worker exception: %1").arg(e.what()));
            }

            QThread::msleep(pollIntervalMs);
        }

        qDebug() << "[ScanWorker] Thread stopped";
    }

    void ScanWorker::processDirtyPins() {
        std::lock_guard<std::mutex> lock(dirtyMutex);
        for (const auto& [cellIndex, level] : dirtyPins) {
            engine->setPin(cellIndex, level);
        }
        dirtyPins.clear();
    }

    void ScanWorker::applyMode(ScanMode mode) {
        if (!deviceModel) return;

        std::string instrName;
        switch (mode) {
        case ScanMode::SAMPLE: instrName = "SAMPLE"; break;
        case ScanMode::EXTEST: instrName = "EXTEST"; break;
        case ScanMode::INTEST: instrName = "INTEST"; break;
        case ScanMode::BYPASS: instrName = "BYPASS"; break;
        }

        uint32_t opcode = deviceModel->getInstruction(instrName);

        // Fallback común para SAMPLE
        if (mode == ScanMode::SAMPLE && opcode == 0xFFFFFFFF) {
            opcode = deviceModel->getInstruction("SAMPLE/PRELOAD");
        }

        if (opcode == 0xFFFFFFFF) {
            qWarning() << "[ScanWorker] Instruction not found in BSDL:" << QString::fromStdString(instrName);
            return;
        }

        size_t irLen = deviceModel->getIRLength();
        qDebug() << "[ScanWorker] Switching to" << QString::fromStdString(instrName)
            << "(Opcode:" << Qt::hex << opcode << ")";

        engine->loadInstruction(opcode, irLen);
    }

} // namespace JTAG