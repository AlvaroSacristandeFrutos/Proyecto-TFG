#pragma once

#include "../IJTAGAdapter.h"
#include <cstdint>
#include <vector>
#include <string>

namespace JTAG {

    class MockAdapter : public IJTAGAdapter {
    public:
        MockAdapter() = default;
        ~MockAdapter() override;

        bool open() override;
        void close() override;
        bool isConnected() const override;

        // Simulación inteligente
        bool shiftData(const std::vector<uint8_t>& tdi,
            std::vector<uint8_t>& tdo,
            size_t numBits,
            bool exitShift = true) override;

        bool writeTMS(const std::vector<bool>& tmsSequence) override;
        bool resetTAP() override;

        std::string getName() const override { return "Mock JTAG Simulator"; }
        uint32_t getClockSpeed() const override { return clockSpeed; }
        bool setClockSpeed(uint32_t speedHz) override;
        std::string getInfo() const override { return "Simulation: IDCODE + Walking Bits"; }

    private:
        bool connected = false;
        uint32_t clockSpeed = 1000000;

        // Estado de la simulación
        uint8_t simulationCounter = 0; // Para generar patrones cambiantes
    };

} // namespace JTAG