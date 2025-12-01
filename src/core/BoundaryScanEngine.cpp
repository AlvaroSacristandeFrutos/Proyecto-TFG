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
            bsr.resize(numBytes, 0);
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

    TAPState BoundaryScanEngine::getNextState(TAPState current, bool tms) const {
        // Delegamos la lógica a la StateMachine central
        return JtagStateMachine::nextState(current, tms);
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

        if (!gotoState(TAPState::SHIFT_IR)) return false;

        size_t numBytes = (irLength + 7) / 8;
        std::vector<uint8_t> tdi(numBytes, 0);
        std::vector<uint8_t> tdo;

        for (size_t i = 0; i < numBytes; i++) {
            tdi[i] = (instruction >> (i * 8)) & 0xFF;
        }

        if (!adapter->shiftData(tdi, tdo, irLength, true)) return false; // Exit1-IR

        currentState = TAPState::EXIT1_IR;
        if (!gotoState(TAPState::UPDATE_IR)) return false;
        if (!gotoState(TAPState::RUN_TEST_IDLE)) return false;

        return true;
    }

    uint32_t BoundaryScanEngine::readIDCODE() {
        std::cout << "BoundaryScanEngine::readIDCODE()\n";

        if (!reset()) return 0;
        if (!gotoState(TAPState::SHIFT_DR)) return 0;

        std::vector<uint8_t> tdi(4, 0xFF);
        std::vector<uint8_t> tdo;

        if (!adapter->shiftData(tdi, tdo, 32, true)) return 0; // Exit1-DR

        currentState = TAPState::EXIT1_DR;
        gotoState(TAPState::RUN_TEST_IDLE);

        uint32_t idcode = 0;
        for (size_t i = 0; i < 4; i++) {
            idcode |= (static_cast<uint32_t>(tdo[i]) << (i * 8));
        }
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

    bool BoundaryScanEngine::applyChanges() {
        if (bsrLength == 0) return false;
        if (!gotoState(TAPState::SHIFT_DR)) return false;

        std::vector<uint8_t> tdo;
        if (!adapter->shiftData(bsr, tdo, bsrLength, true)) return false;

        currentState = TAPState::EXIT1_DR;
        if (!gotoState(TAPState::UPDATE_DR)) return false;
        if (!gotoState(TAPState::RUN_TEST_IDLE)) return false;
        return true;
    }

    bool BoundaryScanEngine::samplePins() {
        if (bsrLength == 0) return false;
        if (!gotoState(TAPState::SHIFT_DR)) return false;

        std::vector<uint8_t> tdo;
        if (!adapter->shiftData(bsr, tdo, bsrLength, true)) return false;

        bsr = tdo;
        currentState = TAPState::EXIT1_DR;
        if (!gotoState(TAPState::UPDATE_DR)) return false;
        if (!gotoState(TAPState::RUN_TEST_IDLE)) return false;
        return true;
    }

    bool BoundaryScanEngine::setBSR(const std::vector<uint8_t>& data) {
        size_t numBytes = (bsrLength + 7) / 8;
        if (data.size() != numBytes) return false;
        bsr = data;
        return true;
    }

} // namespace JTAG