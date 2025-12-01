#include "JtagStateMachine.h"
#include <iostream>

namespace JTAG {

    // ============================================================================
    // HELPERS
    // ============================================================================

    std::string tapStateToString(TAPState state) {
        switch (state) {
        case TAPState::TEST_LOGIC_RESET: return "TEST_LOGIC_RESET";
        case TAPState::RUN_TEST_IDLE:    return "RUN_TEST_IDLE";
        case TAPState::SELECT_DR_SCAN:   return "SELECT_DR_SCAN";
        case TAPState::CAPTURE_DR:       return "CAPTURE_DR";
        case TAPState::SHIFT_DR:         return "SHIFT_DR";
        case TAPState::EXIT1_DR:         return "EXIT1_DR";
        case TAPState::PAUSE_DR:         return "PAUSE_DR";
        case TAPState::EXIT2_DR:         return "EXIT2_DR";
        case TAPState::UPDATE_DR:        return "UPDATE_DR";
        case TAPState::SELECT_IR_SCAN:   return "SELECT_IR_SCAN";
        case TAPState::CAPTURE_IR:       return "CAPTURE_IR";
        case TAPState::SHIFT_IR:         return "SHIFT_IR";
        case TAPState::EXIT1_IR:         return "EXIT1_IR";
        case TAPState::PAUSE_IR:         return "PAUSE_IR";
        case TAPState::EXIT2_IR:         return "EXIT2_IR";
        case TAPState::UPDATE_IR:        return "UPDATE_IR";
        default:                         return "UNKNOWN";
        }
    }

    // ============================================================================
    // TABLA DE TRANSICIONES OPTIMIZADA (IEEE 1149.1)
    // ============================================================================

    // Macro para definir rutas: {bits_tms, numero_bits}
    // Nota: Los bits se envían LSB primero (bit 0, luego bit 1...)
#define P(bits, count) {bits, count}

    const JtagPath JtagStateMachine::lookupTable[16][16] = {
        // [0] FROM: TEST_LOGIC_RESET
        // To: TL_RESET, IDLE,       SEL_DR,     CAP_DR,     SHIFT_DR,   EXIT1_DR,   PAUSE_DR,   EXIT2_DR,   UPD_DR,     SEL_IR,     CAP_IR,     SHIFT_IR,   EXIT1_IR,   PAUSE_IR,   EXIT2_IR,   UPD_IR
        { P(0x01,1), P(0x00,1), P(0x02,2), P(0x02,3), P(0x02,4), P(0x0a,4), P(0x0a,5), P(0x2a,6), P(0x1a,5), P(0x06,3), P(0x06,4), P(0x06,5), P(0x0e,5), P(0x0e,6), P(0x2e,7), P(0x1e,6) },

        // [1] FROM: RUN_TEST_IDLE
        // To: TL_RESET, IDLE,       SEL_DR,     CAP_DR,     SHIFT_DR,   EXIT1_DR,   PAUSE_DR,   EXIT2_DR,   UPD_DR,     SEL_IR,     CAP_IR,     SHIFT_IR,   EXIT1_IR,   PAUSE_IR,   EXIT2_IR,   UPD_IR
        { P(0x07,3), P(0x00,0), P(0x01,1), P(0x01,2), P(0x01,3), P(0x05,3), P(0x05,4), P(0x15,5), P(0x0d,4), P(0x03,2), P(0x03,3), P(0x03,4), P(0x07,4), P(0x07,5), P(0x17,6), P(0x0f,5) },

        // [2] FROM: SELECT_DR_SCAN
        // To: TL_RESET, IDLE,       SEL_DR,     CAP_DR,     SHIFT_DR,   EXIT1_DR,   PAUSE_DR,   EXIT2_DR,   UPD_DR,     SEL_IR,     CAP_IR,     SHIFT_IR,   EXIT1_IR,   PAUSE_IR,   EXIT2_IR,   UPD_IR
        { P(0x03,2), P(0x03,3), P(0x00,0), P(0x00,1), P(0x00,2), P(0x02,2), P(0x02,3), P(0x0a,4), P(0x06,3), P(0x01,1), P(0x01,2), P(0x01,3), P(0x03,3), P(0x03,4), P(0x0b,5), P(0x07,4) },

        // [3] FROM: CAPTURE_DR
        // To: TL_RESET, IDLE,       SEL_DR,     CAP_DR,     SHIFT_DR,   EXIT1_DR,   PAUSE_DR,   EXIT2_DR,   UPD_DR,     SEL_IR,     CAP_IR,     SHIFT_IR,   EXIT1_IR,   PAUSE_IR,   EXIT2_IR,   UPD_IR
        { P(0x1f,5), P(0x03,3), P(0x07,3), P(0x00,0), P(0x00,1), P(0x01,1), P(0x01,2), P(0x05,3), P(0x03,2), P(0x0f,4), P(0x0f,5), P(0x0f,6), P(0x1f,6), P(0x1f,7), P(0x5f,8), P(0x3f,7) },

        // [4] FROM: SHIFT_DR
        // To: TL_RESET, IDLE,       SEL_DR,     CAP_DR,     SHIFT_DR,   EXIT1_DR,   PAUSE_DR,   EXIT2_DR,   UPD_DR,     SEL_IR,     CAP_IR,     SHIFT_IR,   EXIT1_IR,   PAUSE_IR,   EXIT2_IR,   UPD_IR
        { P(0x1f,5), P(0x03,3), P(0x07,3), P(0x07,4), P(0x00,0), P(0x01,1), P(0x01,2), P(0x05,3), P(0x03,2), P(0x0f,4), P(0x0f,5), P(0x0f,6), P(0x1f,6), P(0x1f,7), P(0x5f,8), P(0x3f,7) },

        // [5] FROM: EXIT1_DR
        // To: TL_RESET, IDLE,       SEL_DR,     CAP_DR,     SHIFT_DR,   EXIT1_DR,   PAUSE_DR,   EXIT2_DR,   UPD_DR,     SEL_IR,     CAP_IR,     SHIFT_IR,   EXIT1_IR,   PAUSE_IR,   EXIT2_IR,   UPD_IR
        { P(0x0f,4), P(0x01,2), P(0x03,2), P(0x03,3), P(0x03,4), P(0x00,0), P(0x00,1), P(0x02,2), P(0x01,1), P(0x07,3), P(0x07,4), P(0x07,5), P(0x0f,5), P(0x0f,6), P(0x2f,7), P(0x1f,6) },

        // [6] FROM: PAUSE_DR
        // To: TL_RESET, IDLE,       SEL_DR,     CAP_DR,     SHIFT_DR,   EXIT1_DR,   PAUSE_DR,   EXIT2_DR,   UPD_DR,     SEL_IR,     CAP_IR,     SHIFT_IR,   EXIT1_IR,   PAUSE_IR,   EXIT2_IR,   UPD_IR
        { P(0x1f,5), P(0x03,3), P(0x07,3), P(0x07,4), P(0x01,2), P(0x05,3), P(0x00,0), P(0x01,1), P(0x03,2), P(0x0f,4), P(0x0f,5), P(0x0f,6), P(0x1f,6), P(0x1f,7), P(0x5f,8), P(0x3f,7) },

        // [7] FROM: EXIT2_DR
        // To: TL_RESET, IDLE,       SEL_DR,     CAP_DR,     SHIFT_DR,   EXIT1_DR,   PAUSE_DR,   EXIT2_DR,   UPD_DR,     SEL_IR,     CAP_IR,     SHIFT_IR,   EXIT1_IR,   PAUSE_IR,   EXIT2_IR,   UPD_IR
        { P(0x0f,4), P(0x01,2), P(0x03,2), P(0x03,3), P(0x00,1), P(0x02,2), P(0x02,3), P(0x00,0), P(0x01,1), P(0x07,3), P(0x07,4), P(0x07,5), P(0x0f,5), P(0x0f,6), P(0x2f,7), P(0x1f,6) },

        // [8] FROM: UPDATE_DR
        // To: TL_RESET, IDLE,       SEL_DR,     CAP_DR,     SHIFT_DR,   EXIT1_DR,   PAUSE_DR,   EXIT2_DR,   UPD_DR,     SEL_IR,     CAP_IR,     SHIFT_IR,   EXIT1_IR,   PAUSE_IR,   EXIT2_IR,   UPD_IR
        { P(0x07,3), P(0x00,1), P(0x01,1), P(0x01,2), P(0x01,3), P(0x05,3), P(0x05,4), P(0x15,5), P(0x00,0), P(0x03,2), P(0x03,3), P(0x03,4), P(0x07,4), P(0x07,5), P(0x17,6), P(0x0f,5) },

        // [9] FROM: SELECT_IR_SCAN
        // To: TL_RESET, IDLE,       SEL_DR,     CAP_DR,     SHIFT_DR,   EXIT1_DR,   PAUSE_DR,   EXIT2_DR,   UPD_DR,     SEL_IR,     CAP_IR,     SHIFT_IR,   EXIT1_IR,   PAUSE_IR,   EXIT2_IR,   UPD_IR
        { P(0x01,1), P(0x01,2), P(0x05,3), P(0x05,4), P(0x05,5), P(0x15,5), P(0x15,6), P(0x55,7), P(0x35,6), P(0x00,0), P(0x00,1), P(0x00,2), P(0x02,2), P(0x02,3), P(0x0a,4), P(0x06,3) },

        // [10] FROM: CAPTURE_IR
        // To: TL_RESET, IDLE,       SEL_DR,     CAP_DR,     SHIFT_DR,   EXIT1_DR,   PAUSE_DR,   EXIT2_DR,   UPD_DR,     SEL_IR,     CAP_IR,     SHIFT_IR,   EXIT1_IR,   PAUSE_IR,   EXIT2_IR,   UPD_IR
        { P(0x1f,5), P(0x03,3), P(0x07,3), P(0x07,4), P(0x07,5), P(0x17,5), P(0x17,6), P(0x57,7), P(0x37,6), P(0x0f,4), P(0x00,0), P(0x00,1), P(0x01,1), P(0x01,2), P(0x05,3), P(0x03,2) },

        // [11] FROM: SHIFT_IR
        // To: TL_RESET, IDLE,       SEL_DR,     CAP_DR,     SHIFT_DR,   EXIT1_DR,   PAUSE_DR,   EXIT2_DR,   UPD_DR,     SEL_IR,     CAP_IR,     SHIFT_IR,   EXIT1_IR,   PAUSE_IR,   EXIT2_IR,   UPD_IR
        { P(0x1f,5), P(0x03,3), P(0x07,3), P(0x07,4), P(0x07,5), P(0x17,5), P(0x17,6), P(0x57,7), P(0x37,6), P(0x0f,4), P(0x07,4), P(0x00,0), P(0x01,1), P(0x01,2), P(0x05,3), P(0x03,2) },

        // [12] FROM: EXIT1_IR
        // To: TL_RESET, IDLE,       SEL_DR,     CAP_DR,     SHIFT_DR,   EXIT1_DR,   PAUSE_DR,   EXIT2_DR,   UPD_DR,     SEL_IR,     CAP_IR,     SHIFT_IR,   EXIT1_IR,   PAUSE_IR,   EXIT2_IR,   UPD_IR
        { P(0x0f,4), P(0x01,2), P(0x03,2), P(0x03,3), P(0x03,4), P(0x0b,4), P(0x0b,5), P(0x2b,6), P(0x1b,5), P(0x07,3), P(0x03,3), P(0x03,4), P(0x00,0), P(0x00,1), P(0x02,2), P(0x01,1) },

        // [13] FROM: PAUSE_IR
        // To: TL_RESET, IDLE,       SEL_DR,     CAP_DR,     SHIFT_DR,   EXIT1_DR,   PAUSE_DR,   EXIT2_DR,   UPD_DR,     SEL_IR,     CAP_IR,     SHIFT_IR,   EXIT1_IR,   PAUSE_IR,   EXIT2_IR,   UPD_IR
        { P(0x1f,5), P(0x03,3), P(0x07,3), P(0x07,4), P(0x07,5), P(0x17,5), P(0x17,6), P(0x57,7), P(0x37,6), P(0x0f,4), P(0x0f,5), P(0x01,2), P(0x05,3), P(0x00,0), P(0x01,1), P(0x03,2) },

        // [14] FROM: EXIT2_IR
        // To: TL_RESET, IDLE,       SEL_DR,     CAP_DR,     SHIFT_DR,   EXIT1_DR,   PAUSE_DR,   EXIT2_DR,   UPD_DR,     SEL_IR,     CAP_IR,     SHIFT_IR,   EXIT1_IR,   PAUSE_IR,   EXIT2_IR,   UPD_IR
        { P(0x0f,4), P(0x01,2), P(0x03,2), P(0x03,3), P(0x03,4), P(0x0b,4), P(0x0b,5), P(0x2b,6), P(0x1b,5), P(0x07,3), P(0x07,4), P(0x00,1), P(0x02,2), P(0x02,3), P(0x00,0), P(0x01,1) },

        // [15] FROM: UPDATE_IR
        // To: TL_RESET, IDLE,       SEL_DR,     CAP_DR,     SHIFT_DR,   EXIT1_DR,   PAUSE_DR,   EXIT2_DR,   UPD_DR,     SEL_IR,     CAP_IR,     SHIFT_IR,   EXIT1_IR,   PAUSE_IR,   EXIT2_IR,   UPD_IR
        { P(0x07,3), P(0x00,1), P(0x01,1), P(0x01,2), P(0x01,3), P(0x05,3), P(0x05,4), P(0x15,5), P(0x0d,4), P(0x03,2), P(0x03,3), P(0x03,4), P(0x07,4), P(0x07,5), P(0x17,6), P(0x00,0) }
    };

    JtagPath JtagStateMachine::getPath(TAPState from, TAPState to) {
        return lookupTable[static_cast<int>(from)][static_cast<int>(to)];
    }

    TAPState JtagStateMachine::nextState(TAPState current, bool tms) {
        switch (current) {
        case TAPState::TEST_LOGIC_RESET: return tms ? TAPState::TEST_LOGIC_RESET : TAPState::RUN_TEST_IDLE;
        case TAPState::RUN_TEST_IDLE:    return tms ? TAPState::SELECT_DR_SCAN : TAPState::RUN_TEST_IDLE;
        case TAPState::SELECT_DR_SCAN:   return tms ? TAPState::SELECT_IR_SCAN : TAPState::CAPTURE_DR;
        case TAPState::CAPTURE_DR:       return tms ? TAPState::EXIT1_DR : TAPState::SHIFT_DR;
        case TAPState::SHIFT_DR:         return tms ? TAPState::EXIT1_DR : TAPState::SHIFT_DR;
        case TAPState::EXIT1_DR:         return tms ? TAPState::UPDATE_DR : TAPState::PAUSE_DR;
        case TAPState::PAUSE_DR:         return tms ? TAPState::EXIT2_DR : TAPState::PAUSE_DR;
        case TAPState::EXIT2_DR:         return tms ? TAPState::UPDATE_DR : TAPState::SHIFT_DR;
        case TAPState::UPDATE_DR:        return tms ? TAPState::SELECT_DR_SCAN : TAPState::RUN_TEST_IDLE;
        case TAPState::SELECT_IR_SCAN:   return tms ? TAPState::TEST_LOGIC_RESET : TAPState::CAPTURE_IR;
        case TAPState::CAPTURE_IR:       return tms ? TAPState::EXIT1_IR : TAPState::SHIFT_IR;
        case TAPState::SHIFT_IR:         return tms ? TAPState::EXIT1_IR : TAPState::SHIFT_IR;
        case TAPState::EXIT1_IR:         return tms ? TAPState::UPDATE_IR : TAPState::PAUSE_IR;
        case TAPState::PAUSE_IR:         return tms ? TAPState::EXIT2_IR : TAPState::PAUSE_IR;
        case TAPState::EXIT2_IR:         return tms ? TAPState::UPDATE_IR : TAPState::SHIFT_IR;
        case TAPState::UPDATE_IR:        return tms ? TAPState::SELECT_DR_SCAN : TAPState::RUN_TEST_IDLE;
        default: return TAPState::TEST_LOGIC_RESET;
        }
    }

} // namespace JTAG