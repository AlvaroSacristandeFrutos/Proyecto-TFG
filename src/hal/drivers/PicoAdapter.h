#pragma once

#include "../IJTAGAdapter.h"
#include "../JtagProtocol.h"
#include <vector>
#include <string>

namespace JTAG {

class PicoAdapter : public IJTAGAdapter {
public:
    PicoAdapter();
    ~PicoAdapter() override;

    // --- Implementación de IJTAGAdapter ---
    bool open() override;
    void close() override;
    bool isConnected() const override;

    bool shiftData(const std::vector<uint8_t>& tdi,
                   std::vector<uint8_t>& tdo,
                   size_t numBits,
                   bool exitShift = true) override;

    bool writeTMS(const std::vector<bool>& tmsSequence) override;
    bool resetTAP() override;

    // Métodos de alto nivel (transaccionales) - STUBS (requiere firmware)
    bool scanIR(uint8_t irLength, const std::vector<uint8_t>& dataIn,
                std::vector<uint8_t>& dataOut) override;
    bool scanDR(size_t drLength, const std::vector<uint8_t>& dataIn,
                std::vector<uint8_t>& dataOut) override;
    uint32_t readIDCODE() override;

    // Métodos de configuración
    std::string getName() const override { return "Raspberry Pi Pico Probe"; }
    uint32_t getClockSpeed() const override { return clockSpeed; }
    bool setClockSpeed(uint32_t speedHz) override;
    std::string getInfo() const override { return "JTAG over USB-CDC (TinyUSB)"; }

private:
    bool connected = false;
    uint32_t clockSpeed = 1000000;

    // TODO: Aquí iría el objeto de Puerto Serie real (ej: QSerialPort o Handle de Windows)
    // void* serialHandle = nullptr; 

    // Helper interno para enviar y recibir paquetes usando el protocolo
    bool transceivePacket(JtagCommand cmd, const std::vector<uint8_t>& payload, std::vector<uint8_t>& responsePayload);
};

} // namespace JTAG