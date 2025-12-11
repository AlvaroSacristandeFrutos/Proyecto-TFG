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

        while (running) {
            try {
                // =================================================================
                // CORRECCIÓN CRÍTICA: SIEMPRE CARGAR SAMPLE ANTES DE LEER
                // Esto "machaca" el IDCODE si el chip se ha reseteado.
                // =================================================================
                if (deviceModel && !inEXTESTMode) {
                    uint32_t opcode = deviceModel->getInstruction("SAMPLE");
                    // Si no encuentra SAMPLE, prueba SAMPLE/PRELOAD
                    if (opcode == 0xFFFFFFFF) opcode = deviceModel->getInstruction("SAMPLE/PRELOAD");

                    size_t irLen = deviceModel->getIRLength();
                    engine->loadInstruction(opcode, irLen);
                }
                // =================================================================

                // Modo Escritura (EXTEST)
                if (hasDirtyPins()) {
                    if (!inEXTESTMode) {
                        switchToEXTEST();
                        inEXTESTMode = true;
                    }
                    processDirtyPins();
                    engine->applyChanges();
                }
                // Modo Lectura (SAMPLE) -> Ya forzado arriba si no estamos en EXTEST
                else {
                    inEXTESTMode = false;
                }

                // LEER DATOS (Sample)
                if (!engine->samplePins()) {
                    emit errorOccurred("Failed to sample pins");
                    QThread::msleep(1000);
                    continue;
                }

                // Construir vector de resultados
                std::vector<PinLevel> pins;
                size_t bsrLen = engine->getBSRLength();
                pins.reserve(bsrLen);

                for (size_t i = 0; i < bsrLen; ++i) {
                    auto level = engine->getPin(i);
                    pins.push_back(level.value_or(PinLevel::HIGH_Z));
                }

                emit pinsUpdated(pins);

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

    void ScanWorker::switchToEXTEST() {
        qDebug() << "[ScanWorker] Switching to EXTEST mode";
        if (!deviceModel) return;

        // CORREGIDO: Usar longitud real, no '5' hardcodeado
        uint32_t opcode = deviceModel->getInstruction("EXTEST");
        size_t irLen = deviceModel->getIRLength();

        engine->loadInstruction(opcode, irLen);
    }

    void ScanWorker::switchToSAMPLE() {
        // Esta función ya no es crítica porque lo hacemos en el bucle,
        // pero la mantenemos correcta por si acaso.
        qDebug() << "[ScanWorker] Switching to SAMPLE mode";
        if (!deviceModel) return;

        uint32_t opcode = deviceModel->getInstruction("SAMPLE");
        size_t irLen = deviceModel->getIRLength();

        if (opcode == 0xFFFFFFFF) opcode = deviceModel->getInstruction("SAMPLE/PRELOAD");

        engine->loadInstruction(opcode, irLen);
    }

} // namespace JTAG