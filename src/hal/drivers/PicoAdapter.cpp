#include "PicoAdapter.h"
#include <iostream>
#include <thread>
#include <chrono>

//ESTÁ STUBBEADO DE MOMENTO

namespace JTAG {

    PicoAdapter::PicoAdapter() {
    }

    PicoAdapter::~PicoAdapter() {
        close();
    }

    bool PicoAdapter::open() {
        // AQUÍ: Abrir puerto COM real (ej: COM3 o /dev/ttyACM0)
        std::cout << "[PicoAdapter] Opening serial port...\n";

        // Simulamos que enviamos un PING para ver si hay una Pico al otro lado
        std::vector<uint8_t> response;
        if (!transceivePacket(JtagCommand::CMD_PING, {}, response)) {
            std::cerr << "[PicoAdapter] Handshake failed\n";
            return false;
        }

        connected = true;
        return true;
    }

    void PicoAdapter::close() {
        if (connected) {
            std::cout << "[PicoAdapter] Closing connection\n";
            connected = false;
        }
    }

    bool PicoAdapter::isConnected() const {
        return connected;
    }

    bool PicoAdapter::setClockSpeed(uint32_t speedHz) {
        // Crear payload de 4 bytes (uint32 little endian)
        std::vector<uint8_t> payload(4);
        payload[0] = speedHz & 0xFF;
        payload[1] = (speedHz >> 8) & 0xFF;
        payload[2] = (speedHz >> 16) & 0xFF;
        payload[3] = (speedHz >> 24) & 0xFF;

        std::vector<uint8_t> response;
        if (transceivePacket(JtagCommand::CMD_SET_CLOCK, payload, response)) {
            clockSpeed = speedHz;
            return true;
        }
        return false;
    }

    // --------------------------------------------------------------------------
    // IMPLEMENTACIÓN CRÍTICA: SHIFT DATA
    // --------------------------------------------------------------------------
    bool PicoAdapter::shiftData(const std::vector<uint8_t>& tdi,
        std::vector<uint8_t>& tdo,
        size_t numBits,
        bool exitShift)
    {
        if (!connected) return false;

        // 1. Construir Payload para CMD_SHIFT_DATA
        // Estructura: [NumBits(4)] + [ExitShift(1)] + [TDI_Data(N)]
        std::vector<uint8_t> payload;
        payload.reserve(5 + tdi.size());

        // NumBits (32-bit Little Endian)
        payload.push_back(numBits & 0xFF);
        payload.push_back((numBits >> 8) & 0xFF);
        payload.push_back((numBits >> 16) & 0xFF);
        payload.push_back((numBits >> 24) & 0xFF);

        // Flags (ExitShift)
        payload.push_back(exitShift ? 1 : 0);

        // Datos TDI
        payload.insert(payload.end(), tdi.begin(), tdi.end());

        // 2. Transacción
        std::vector<uint8_t> response;
        if (!transceivePacket(JtagCommand::CMD_SHIFT_DATA, payload, response)) {
            return false;
        }

        // 3. Copiar respuesta (TDO) al buffer de salida
        tdo = response;
        return true;
    }

    bool PicoAdapter::writeTMS(const std::vector<bool>& tmsSequence) {
        if (!connected) return false;

        // Empaquetar bools en bytes
        size_t numBits = tmsSequence.size();
        size_t numBytes = (numBits + 7) / 8;
        std::vector<uint8_t> tmsBytes(numBytes, 0);

        for (size_t i = 0; i < numBits; ++i) {
            if (tmsSequence[i]) {
                tmsBytes[i / 8] |= (1 << (i % 8));
            }
        }

        // Payload: [NumBits(1)] + [TMS_Bytes(N)]
        // Nota: El protocolo define NumBits como 1 byte para TMS porque las secuencias son cortas
        std::vector<uint8_t> payload;
        payload.push_back(static_cast<uint8_t>(numBits));
        payload.insert(payload.end(), tmsBytes.begin(), tmsBytes.end());

        std::vector<uint8_t> dummy;
        return transceivePacket(JtagCommand::CMD_WRITE_TMS, payload, dummy);
    }

    bool PicoAdapter::resetTAP() {
        std::vector<uint8_t> dummy;
        // Envía comando dedicado RESET
        return transceivePacket(JtagCommand::CMD_RESET_TAP, {}, dummy);
    }

    // --------------------------------------------------------------------------
    // CAPA DE TRANSPORTE (SIMULADA POR AHORA)
    // --------------------------------------------------------------------------
    bool PicoAdapter::transceivePacket(JtagCommand cmd,
        const std::vector<uint8_t>& payload,
        std::vector<uint8_t>& responsePayload)
    {
        // 1. Construir paquete binario completo usando JtagProtocol.h
        std::vector<uint8_t> packet = buildPacket(cmd, payload);

        // DEBUG: Ver qué estamos enviando
        /*
        std::cout << "[TX] Cmd: 0x" << std::hex << (int)cmd << " Len: " << payload.size() << std::dec << "\n";
        */

        // TODO: serial.write(packet);
        // TODO: serial.read(header); -> parse length -> serial.read(payload + crc);

        // --- SIMULACIÓN DE RESPUESTA ---
        // Simulamos que la Pico responde OK o devuelve datos (Loopback)
        std::this_thread::sleep_for(std::chrono::milliseconds(1)); // Simular latencia USB

        if (cmd == JtagCommand::CMD_PING) {
            // Ping OK
            return true;
        }
        else if (cmd == JtagCommand::CMD_SHIFT_DATA) {
            // En modo loopback simulado, TDO = TDI
            // El payload enviado tiene 5 bytes de cabecera (bits+flags) que ignoramos para el loopback
            if (payload.size() > 5) {
                responsePayload.assign(payload.begin() + 5, payload.end());
            }
            return true;
        }

        return true; // Asumir éxito por defecto
    }

} // namespace JTAG