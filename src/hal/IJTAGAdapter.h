#pragma once

#include <vector>
#include <string>
#include <cstdint>
#include <memory>

namespace JTAG {

    // 1. Definir el Enum PRIMERO
    enum class AdapterType {
        MOCK,
        PICO,
        FT2232H, //Tengo que implementarla todavía
        JLINK
    };

    // 2. Definir el Struct de Descriptor SEGUNDO
    struct AdapterDescriptor {
        AdapterType type;
        std::string name;
        std::string serialNumber;
    };

    // 3. Definir la Interfaz TERCERO
    class IJTAGAdapter {
    public:
        virtual ~IJTAGAdapter() = default;

        // ========== PRIMITIVAS DE BAJO NIVEL (mantener por compatibilidad) ==========
        virtual bool shiftData(const std::vector<uint8_t>& tdi,
            std::vector<uint8_t>& tdo,
            size_t numBits,
            bool exitShift = true) = 0;

        virtual bool writeTMS(const std::vector<bool>& tmsSequence) = 0;
        virtual bool resetTAP() = 0;

        // ========== PRIMITIVAS DE ALTO NIVEL (transaccionales) ==========

        // Cargar instrucción en IR
        // - Navega automáticamente: Current → Shift-IR → Update-IR → Run-Test/Idle
        // - Maneja TMS transitions internamente
        virtual bool scanIR(uint8_t irLength, const std::vector<uint8_t>& dataIn,
                            std::vector<uint8_t>& dataOut) = 0;

        // Intercambiar datos con DR
        // - Navega automáticamente: Current → Shift-DR → Update-DR → Run-Test/Idle
        // - Maneja TMS transitions internamente
        virtual bool scanDR(size_t drLength, const std::vector<uint8_t>& dataIn,
                            std::vector<uint8_t>& dataOut) = 0;

        // Leer IDCODE (operación atómica optimizada)
        // - Reset → Shift-DR (IDCODE auto-loaded) → captura 32 bits → Run-Test/Idle
        virtual uint32_t readIDCODE() = 0;

        // ========== MÉTODOS DE GESTIÓN (sin cambios) ==========
        virtual bool open() = 0;
        virtual void close() = 0;
        virtual bool isConnected() const = 0;

        // Información
        virtual std::string getName() const = 0;
        virtual uint32_t getClockSpeed() const = 0;
        virtual bool setClockSpeed(uint32_t speedHz) = 0;
        virtual std::string getInfo() const = 0;
    };

} // namespace JTAG