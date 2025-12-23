#pragma once

// --- INCLUDES OBLIGATORIOS ---
#include <vector>
#include <cstdint> // Para uint8_t
#include <string>  // Para std::string
// -----------------------------

namespace JTAG {

    enum class TAPState : uint8_t {
        TEST_LOGIC_RESET = 0x00,
        RUN_TEST_IDLE = 0x01,
        SELECT_DR_SCAN = 0x02,
        CAPTURE_DR = 0x03,
        SHIFT_DR = 0x04,
        EXIT1_DR = 0x05,
        PAUSE_DR = 0x06,
        EXIT2_DR = 0x07,
        UPDATE_DR = 0x08,
        SELECT_IR_SCAN = 0x09,
        CAPTURE_IR = 0x0A,
        SHIFT_IR = 0x0B,
        EXIT1_IR = 0x0C,
        PAUSE_IR = 0x0D,
        EXIT2_IR = 0x0E,
        UPDATE_IR = 0x0F
    };

    struct JtagPath {
        uint8_t tmsBits; // Ristra de bits a seguir para el destino
        uint8_t bitCount; //Nº de saltos hasta el siguiente estado
    };

    std::string tapStateToString(TAPState state); //Definimos los estados posibles

    class JtagStateMachine {
    public:
        JtagStateMachine() = default;
        ~JtagStateMachine() = default;

        static JtagPath getPath(TAPState from, TAPState to); //Defino la funcion que obtiene el path mas corto para ese estado
        static TAPState nextState(TAPState current, bool tms); //Defino el salto al siguiente

    private:
        static const JtagPath lookupTable[16][16]; //Tabla easttica para mejorar la velocidad y no hacerlo dinamicamente
    };

} // namespace JTAG