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

        while (running) {
            try {
                if (!deviceModel) {
                    QThread::msleep(100);
                    continue;
                }

                ScanMode targetMode = currentMode.load();

                // 1. CARGA DE INSTRUCCIÓN (Persistente)
                // Cargamos la instrucción en cada ciclo para asegurar que el chip
                // no se ha escapado a IDCODE o System Mode por ruido.
                // Al haber quitado el Reset del adaptador, esto es seguro y no parpadea.

                std::string instrName = "SAMPLE"; // Default
                if (targetMode == ScanMode::EXTEST) instrName = "EXTEST";
                if (targetMode == ScanMode::BYPASS) instrName = "BYPASS";

                uint32_t opcode = deviceModel->getInstruction(instrName);

                // Fallback para SAMPLE
                if (opcode == 0xFFFFFFFF && targetMode == ScanMode::SAMPLE)
                    opcode = deviceModel->getInstruction("SAMPLE/PRELOAD");

                size_t irLen = deviceModel->getIRLength();

                // Recargamos la instrucción
                if (!engine->loadInstruction(opcode, irLen)) {
                    // Error silencioso para no saturar logs
                }

                // 2. EJECUCIÓN DEL MODO
                if (targetMode == ScanMode::EXTEST) {
                    // A) Procesar nuevos cambios de la GUI (si los hay)
                    if (hasDirtyPins()) {
                        processDirtyPins();
                    }

                    // B) [CRÍTICO] RESTAURAR SALIDAS FORZADAS
                    // Antes de enviar, re-aplicamos nuestros deseos guardados sobre el BSR.
                    // Esto evita que la lectura anterior ("0") nos borre el "1" que queremos mantener.
                    for (const auto& [cell, level] : desiredOutputs) {
                        engine->setPin(cell, level);
                    }

                    // C) Enviar datos (Shift-DR)
                    // Esto escribe los valores en el chip y mantiene el LED encendido
                    if (!engine->applyChanges()) {
                        emit errorOccurred("Failed to apply changes in EXTEST");
                    }
                }
                else {
                    // Modo SAMPLE (Solo lectura)
                    engine->samplePins();
                }

                // 3. ACTUALIZAR GUI
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
    } // <--- ESTA LLAVE CIERRA LA FUNCIÓN RUN()

    // --------------------------------------------------------------------------
    // FUNCIONES AUXILIARES (FUERA DE RUN)
    // --------------------------------------------------------------------------

    void ScanWorker::processDirtyPins() {
        std::lock_guard<std::mutex> lock(dirtyMutex);
        for (const auto& [cellIndex, level] : dirtyPins) {
            // 1. Guardamos en el Engine (para el ciclo actual)
            engine->setPin(cellIndex, level);

            // 2. Guardamos en nuestra Memoria Persistente (para ciclos futuros)
            // Esto es vital para que applyChanges no borre el valor en la siguiente vuelta
            desiredOutputs[cellIndex] = level;
        }
        dirtyPins.clear();
    }

    // Función auxiliar (aunque ahora hacemos la carga en el bucle, la mantenemos por compatibilidad)
    void ScanWorker::applyMode(ScanMode mode) {
        // La lógica real está ahora integrada en run() para mayor robustez
        Q_UNUSED(mode);
    }

} // namespace JTAG