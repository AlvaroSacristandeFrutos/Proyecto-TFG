#pragma once

// --- INCLUDES OBLIGATORIOS ---
#include <vector>
#include <cstdint>
#include <string>
#include <memory>
#include <optional>
#include <cstddef> // Para size_t
// -----------------------------

#include "../hal/IJTAGAdapter.h"
#include "JtagStateMachine.h"

namespace JTAG {

    enum class PinLevel {
        LOW = 0,
        HIGH = 1,
        HIGH_Z = 2
    };

    class BoundaryScanEngine {
    public:
        explicit BoundaryScanEngine(IJTAGAdapter* adapter, size_t bsrLength = 0);
        ~BoundaryScanEngine() = default;

        // Control TAP
        bool reset();
        bool gotoState(TAPState targetState);
        TAPState getCurrentState() const { return currentState; }

        // JTAG
        bool loadInstruction(uint32_t instruction, size_t irLength = 5);
        uint32_t readIDCODE();
        bool runTestCycles(size_t numCycles);

        // BScan
        void setBSRLength(size_t length);
        size_t getBSRLength() const { return bsrLength; }

        bool setPin(size_t cellIndex, PinLevel level);
        std::optional<PinLevel> getPin(size_t cellIndex) const;

        bool applyChanges();
        bool samplePins();

        const std::vector<uint8_t>& getBSR() const { return bsr; }
        bool setBSR(const std::vector<uint8_t>& data);

        // Target detection - checks if BSR is all 0xFF (no target / pull-ups)
        bool isNoTargetDetected() const;

    private:
        TAPState getNextState(TAPState current, bool tms) const;

        IJTAGAdapter* adapter;
        TAPState currentState;
        size_t bsrLength;
        std::vector<uint8_t> bsr;
    };

} // namespace JTAG