#include "BoundaryScanEngine.h"
#include <iostream>
#include <algorithm>
#include <cstring>

namespace JTAG {

    // NOTA: Hemos eliminado TAP_TRANSITION_TABLE y tapStateToString de aquí.
    // Ahora se usa la lógica centralizada en JtagStateMachine.

    // ============================================================================
    // IMPLEMENTACIÓN DE BoundaryScanEngine
    // ============================================================================

    BoundaryScanEngine::BoundaryScanEngine(IJTAGAdapter* adapter, size_t bsrLength)
        : adapter(adapter)
        , currentState(TAPState::TEST_LOGIC_RESET)
        , bsrLength(bsrLength)
    {
        if (!adapter) {
            throw std::invalid_argument("BoundaryScanEngine: adapter cannot be null");
        }

        if (!adapter->isConnected()) {
            throw std::runtime_error("BoundaryScanEngine: adapter must be connected before creating engine");
        }

        if (bsrLength > 0) {
            size_t numBytes = (bsrLength + 7) / 8;
            bsr.resize(numBytes, 0);         // Buffer de escritura (TDI)
            bsrCapture.resize(numBytes, 0);  // Buffer de lectura (TDO)
        }

        std::cout << "BoundaryScanEngine created (BSR length: " << bsrLength << " bits)\n";
    }

    // ============================================================================
    // CONTROL DEL TAP STATE MACHINE
    // ============================================================================

    bool BoundaryScanEngine::reset() {
        if (!adapter->resetTAP()) {
            std::cerr << "BoundaryScanEngine::reset() - Failed to reset TAP\n";
            return false;
        }

        currentState = TAPState::TEST_LOGIC_RESET;
        std::cout << "BoundaryScanEngine::reset() - TAP reset to TEST_LOGIC_RESET\n";
        return true;
    }

    bool BoundaryScanEngine::resetJTAGStateMachine() {
        // Emergency reset sequence: 5 TMS=1 to reach Test-Logic-Reset,
        // then 1 TMS=0 to move to Run-Test/Idle
        std::vector<bool> tmsSequence = {true, true, true, true, true, false};

        if (!adapter->writeTMS(tmsSequence)) {
            std::cerr << "BoundaryScanEngine::resetJTAGStateMachine() - Failed to send TMS sequence\n";
            return false;
        }

        currentState = TAPState::RUN_TEST_IDLE;
        std::cout << "BoundaryScanEngine::resetJTAGStateMachine() - JTAG TAP reset to RUN_TEST_IDLE\n";
        return true;
    }

    bool BoundaryScanEngine::gotoState(TAPState targetState) {
        if (currentState == targetState) {
            return true;
        }

        // 1. OBTENER RUTA (O(1) usando la Tabla Estática)
        JtagPath path = JtagStateMachine::getPath(currentState, targetState);

        // 2. Debug
        std::cout << "BoundaryScanEngine::gotoState() - "
            << tapStateToString(currentState) << " -> "
            << tapStateToString(targetState) << " (TMS bits: " << (int)path.bitCount << ")\n";

        // 3. Ejecutar Transición
        // Convertimos el byte empaquetado a vector para el adaptador
        std::vector<uint8_t> tmsBytes;
        tmsBytes.push_back(path.tmsBits);

        // Nota: adapter->writeTMS ahora espera (bytes, numBits) o un vector de bools.
        // Como tu IJTAGAdapter::writeTMS actual espera vector<bool>, hacemos la conversión:
        std::vector<bool> tmsSequence;
        for (int i = 0; i < path.bitCount; i++) {
            tmsSequence.push_back((path.tmsBits >> i) & 1);
        }

        if (!adapter->writeTMS(tmsSequence)) {
            std::cerr << "BoundaryScanEngine::gotoState() - Failed to write TMS sequence\n";
            return false;
        }

        currentState = targetState;
        return true;
    }

    // ============================================================================
    // OPERACIONES JTAG BÁSICAS
    // ============================================================================

    bool BoundaryScanEngine::loadInstruction(uint32_t instruction, size_t irLength) {
        std::cout << "BoundaryScanEngine::loadInstruction(0x" << std::hex << instruction
            << std::dec << ", " << irLength << " bits)\n";

        // Preparar datos de entrada
        size_t numBytes = (irLength + 7) / 8;
        std::vector<uint8_t> dataIn(numBytes, 0);

        for (size_t i = 0; i < numBytes; i++) {
            dataIn[i] = (instruction >> (i * 8)) & 0xFF;
        }

        // **DIAGNÓSTICO CRÍTICO**: Mostrar el buffer que se enviará
        std::cout << "  -> Sending IR bytes: 0x";
        for (size_t i = 0; i < numBytes; i++) {
            printf("%02X", dataIn[i]);
        }
        std::cout << "\n";

        // Usar método transaccional de alto nivel
        // El adapter maneja toda la navegación TAP internamente
        std::vector<uint8_t> dataOut;
        if (!adapter->scanIR(static_cast<uint8_t>(irLength), dataIn, dataOut)) {
            std::cerr << "BoundaryScanEngine::loadInstruction() - scanIR failed\n";
            return false;
        }

        // El adapter nos deja en Run-Test/Idle después de scanIR
        currentState = TAPState::RUN_TEST_IDLE;
        return true;
    }

    uint32_t BoundaryScanEngine::readIDCODE() {
        std::cout << "BoundaryScanEngine::readIDCODE()\n";

        // Usar método transaccional de alto nivel
        // El adapter maneja reset TAP, navegación, lectura y retorno a Idle
        uint32_t idcode = adapter->readIDCODE();

        // El adapter nos deja en Run-Test/Idle después de readIDCODE
        currentState = TAPState::RUN_TEST_IDLE;

        return idcode;
    }

    bool BoundaryScanEngine::runTestCycles(size_t numCycles) {
        if (currentState != TAPState::RUN_TEST_IDLE) {
            if (!gotoState(TAPState::RUN_TEST_IDLE)) return false;
        }
        // Usamos writeTMS con ceros para simular reloj en IDLE
        if (numCycles == 0) return true;
        std::vector<bool> idleSeq(numCycles, false);
        return adapter->writeTMS(idleSeq);
    }

    // ============================================================================
    // OPERACIONES BOUNDARY SCAN (Sin cambios lógicos, solo limpieza)
    // ============================================================================

    void BoundaryScanEngine::setBSRLength(size_t length) {
        bsrLength = length;
        size_t numBytes = (length + 7) / 8;
        bsr.resize(numBytes, 0);
        bsrCapture.resize(numBytes, 0);  // NUEVO: inicializar buffer de captura
    }

    bool BoundaryScanEngine::setPin(size_t cellIndex, PinLevel level) {
        if (cellIndex >= bsrLength) return false;
        size_t byteIndex = cellIndex / 8;
        size_t bitIndex = cellIndex % 8;
        if (level == PinLevel::HIGH) bsr[byteIndex] |= (1 << bitIndex);
        else bsr[byteIndex] &= ~(1 << bitIndex);
        return true;
    }

    std::optional<PinLevel> BoundaryScanEngine::getPin(size_t cellIndex) const {
        if (cellIndex >= bsrLength) return std::nullopt;
        size_t byteIndex = cellIndex / 8;
        size_t bitIndex = cellIndex % 8;
        bool bit = (bsr[byteIndex] >> bitIndex) & 1;
        return bit ? PinLevel::HIGH : PinLevel::LOW;
    }

    std::optional<PinLevel> BoundaryScanEngine::getPinReadback(size_t cellIndex) const {
        if (cellIndex >= bsrLength) return std::nullopt;

        size_t byteIndex = cellIndex / 8;
        size_t bitIndex = cellIndex % 8;

        bool bit = (bsrCapture[byteIndex] >> bitIndex) & 1;
        return bit ? PinLevel::HIGH : PinLevel::LOW;
    }

    bool BoundaryScanEngine::applyChanges() {
        // IEEE 1149.1: scanDR ejecuta la secuencia:
        // 1. CAPTURE-DR: El chip captura el estado físico actual de los pines
        // 2. SHIFT-DR: Desplazamos datos (ENVÍA bsr, RECIBE dataOut)
        // 3. UPDATE-DR: El chip aplica el BSR a los pines físicos
        //
        // IMPORTANTE: Con buffers separados:
        // - bsr (TDI) mantiene lo que QUEREMOS escribir (no se sobrescribe)
        // - bsrCapture (TDO) recibe lo que el chip CAPTURÓ
        return scanAndCapture(false);  // NO sobrescribir bsr en EXTEST
    }

    bool BoundaryScanEngine::samplePins() {
        // samplePins() es para LEER el estado del chip
        // Enviamos bsr actual (puede ser cualquier valor)
        // Lo importante es la respuesta en dataOut (TDO)
        //
        // En SAMPLE: safe actualizar bsr
        // En EXTEST/INTEST: NO sobrescribir ediciones del usuario
        return scanAndCapture(true);  // Permitir actualizar bsr si aplica
    }

    bool BoundaryScanEngine::preloadBSR() {
        if (bsrLength == 0) return false;

        // IEEE 1149.1 - SAMPLE/PRELOAD:
        // Este método se llama cuando la instrucción activa es SAMPLE o SAMPLE/PRELOAD.
        // UPDATE-DR carga el "Latch de actualización" del BSR SIN afectar los pines físicos.
        // Los pines solo cambiarán cuando se cargue EXTEST.
        //
        // Propósito: Precargar valores seguros antes de activar EXTEST.

        std::cout << "BoundaryScanEngine::preloadBSR() - Preloading BSR with current values\n";

        std::vector<uint8_t> dataOut;
        if (!adapter->scanDR(bsrLength, bsr, dataOut)) {
            std::cerr << "BoundaryScanEngine::preloadBSR() - scanDR failed\n";
            return false;
        }

        // Guardar respuesta en buffer de captura
        bsrCapture = dataOut;

        // Nota: bsr se mantiene intacto (contiene los valores precargados)

        currentState = TAPState::RUN_TEST_IDLE;

        std::cout << "BoundaryScanEngine::preloadBSR() - Preload successful\n";
        return true;
    }

    bool BoundaryScanEngine::setBSR(const std::vector<uint8_t>& data) {
        size_t numBytes = (bsrLength + 7) / 8;
        if (data.size() != numBytes) return false;
        bsr = data;
        return true;
    }

    bool BoundaryScanEngine::isNoTargetDetected() const {
        if (bsr.empty()) return false;

        // Check if all bytes in BSR are 0xFF (pull-ups - no target)
        for (uint8_t byte : bsr) {
            if (byte != 0xFF) {
                return false; // Found a byte that's not 0xFF
            }
        }

        // All bytes are 0xFF - likely no target connected
        return true;
    }

    // ============================================================================
    // MÉTODO COMÚN PARA ESCANEO Y CAPTURA
    // ============================================================================

    bool BoundaryScanEngine::scanAndCapture(bool updateWriteBuffer) {
        if (bsrLength == 0) return false;

        // DEBUG: Mostrar qué se va a ENVIAR (TDI)
        std::cout << "BSR SENDING (TDI): ";
        for (auto byte : bsr) printf("%02X ", byte);
        std::cout << "\n";

        std::vector<uint8_t> dataOut;
        if (!adapter->scanDR(bsrLength, bsr, dataOut)) {
            std::cerr << "BoundaryScanEngine::scanAndCapture() - scanDR failed\n";
            return false;
        }

        // CRÍTICO: Ciclos de reloj adicionales en RUN-TEST/IDLE después de UPDATE-DR
        // Da tiempo a que los buffers de salida del chip conmuten físicamente
        // Necesario en EXTEST/INTEST para evitar latencia de 1 ciclo
        if (operationMode == OperationMode::EXTEST || operationMode == OperationMode::INTEST) {
            runTestCycles(1);  // 10 ciclos de reloj @ 1 MHz = 10 μs
            //Se puede poner 1 ciclo de reloj y es suficiente, pero es por asegurar. Con 0 sí que falla
        }

        // DEBUG: Mostrar qué se RECIBIÓ (TDO)
        std::cout << "BSR RECEIVED (TDO): ";
        for (auto byte : dataOut) printf("%02X ", byte);
        std::cout << "\n";

        // Guardar en buffer de captura (TDO)
        bsrCapture = dataOut;

        // Actualizar buffer de escritura solo en modos de solo lectura
        if (updateWriteBuffer &&
            (operationMode == OperationMode::SAMPLE || operationMode == OperationMode::BYPASS)) {
            bsr = dataOut;
        }

        currentState = TAPState::RUN_TEST_IDLE;
        return true;
    }

} // namespace JTAG