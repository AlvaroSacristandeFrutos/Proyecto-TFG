#include "ScanWorker.h"
#include <QThread>
#include <QDebug>

namespace JTAG {

ScanWorker::ScanWorker(BoundaryScanEngine* engine, QObject* parent)
    : QObject(parent), engine(engine) {}

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
            // Decisión de modo basada en dirty state
            if (hasDirtyPins()) {
                if (!inEXTESTMode) {
                    switchToEXTEST();
                }
                processDirtyPins();
                engine->applyChanges();  // Escribe al hardware
                inEXTESTMode = true;
            } else {
                if (inEXTESTMode) {
                    switchToSAMPLE();
                    inEXTESTMode = false;
                }
            }

            // Sample siempre (lectura)
            if (!engine->samplePins()) {
                emit errorOccurred("Failed to sample pins");
                QThread::msleep(1000);
                continue;
            }

            // Construir vector de resultados
            std::vector<PinLevel> pins;
            for (size_t i = 0; i < engine->getBSRLength(); ++i) {
                auto level = engine->getPin(i);
                pins.push_back(level.value_or(PinLevel::HIGH_Z));
            }

            emit pinsUpdated(pins);

        } catch (const std::exception& e) {
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
    // Cargar instrucción EXTEST
    // TODO: obtener opcode desde DeviceModel
    engine->loadInstruction(0x00, 5);  // EXTEST típicamente es 0x00
}

void ScanWorker::switchToSAMPLE() {
    qDebug() << "[ScanWorker] Switching to SAMPLE mode";
    // Cargar instrucción SAMPLE
    engine->loadInstruction(0x01, 5);  // SAMPLE típicamente es 0x01
}

} // namespace JTAG
