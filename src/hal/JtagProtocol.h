#pragma once

#include <cstdint>
#include <vector>

namespace JTAG {

    constexpr uint8_t JTAG_PROTOCOL_START_BYTE = 0xA5;

    enum class JtagCommand : uint8_t {
        CMD_PING = 0x01,
        CMD_RESET_TAP = 0x02,
        CMD_SET_CLOCK = 0x03,
        CMD_WRITE_TMS = 0x10,
        CMD_SHIFT_DATA = 0x11,
        RESP_OK = 0x80,
        RESP_DATA = 0x81
        //podríamos añadir más comandos
    };

    

#pragma pack(push, 1) // Inicia empaquetado a 1 byte (sin huecos)

/**
 * @brief Cabecera de paquete (5 bytes)
 */
    struct PacketHeader {
        uint8_t  startByte;   ///< Siempre 0xA5
        uint8_t  command;     ///< JtagCommand
        uint16_t length;      ///< Longitud del payload (little-endian)
    }; // Ya no usamos __attribute__((packed)) aquí

    /**
     * @brief Estadísticas del firmware
     */
    struct FirmwareStats {
        uint32_t totalCommands;
        uint32_t totalBitsShifted;
        uint32_t errorCount;
        uint32_t uptimeMs;
        uint8_t  tapState;
        uint8_t  firmwareVersion;
    };

#pragma pack(pop) // Restaura la alineación original

    // ==============================================================================
    // FUNCIONES DE UTILIDAD (Mantener igual)
    // ==============================================================================

    inline uint8_t calculateCRC8(const uint8_t* data, size_t length) {
        uint8_t crc = 0x00;
        for (size_t i = 0; i < length; ++i) {
            crc ^= data[i];
            for (uint8_t j = 0; j < 8; ++j) {
                if (crc & 0x80) crc = (crc << 1) ^ 0x07;
                else crc <<= 1;
            }
        }
        return crc;
    }

    inline std::vector<uint8_t> buildPacket(JtagCommand cmd, const std::vector<uint8_t>& payload = {}) {
        std::vector<uint8_t> packet;
        packet.reserve(5 + payload.size() + 1);

        packet.push_back(JTAG_PROTOCOL_START_BYTE);
        packet.push_back(static_cast<uint8_t>(cmd));

        uint16_t len = static_cast<uint16_t>(payload.size());
        packet.push_back(len & 0xFF);
        packet.push_back((len >> 8) & 0xFF);

        packet.insert(packet.end(), payload.begin(), payload.end());

        uint8_t crc = calculateCRC8(packet.data(), packet.size());
        packet.push_back(crc);

        return packet;
    }

} // namespace JTAG