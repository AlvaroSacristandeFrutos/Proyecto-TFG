#include "JLinkAdapter.h"
#include <iostream>
#include <vector>
#include <cstring>

namespace JTAG {

    // --- CONSTANTE DE NOMBRE DE LIBRERÍA ---
#if defined(_WIN32)
#if defined(_WIN64)
    const char* JLINK_LIB_NAME = "JLink_x64.dll";
#else
    const char* JLINK_LIB_NAME = "JLinkARM.dll";
#endif
#else
    const char* JLINK_LIB_NAME = "./libjlinkarm.so";
#endif
    // ---------------------------------------

    JLinkAdapter::JLinkAdapter() {}

    JLinkAdapter::~JLinkAdapter() {
        close();
    }

    // Método estático para verificar si la DLL existe en el sistema
    bool JLinkAdapter::isLibraryAvailable() {
#if defined(_WIN32)
        HMODULE h = LoadLibraryA(JLINK_LIB_NAME);
        if (h) {
            FreeLibrary(h);
            return true;
        }
#else
        void* h = dlopen(JLINK_LIB_NAME, RTLD_LAZY);
        if (h) {
            dlclose(h);
            return true;
        }
#endif
        return false;
    }

    bool JLinkAdapter::loadLibrary() {
        if (libHandle) return true;

#if defined(_WIN32)
        libHandle = LoadLibraryA(JLINK_LIB_NAME);
#else
        libHandle = dlopen(JLINK_LIB_NAME, RTLD_NOW);
#endif

        if (!libHandle) {
            // No imprimimos error aquí para no ensuciar si solo estamos detectando
            return false;
        }

        // Cargar símbolos
        pJLINK_OpenEx = reinterpret_cast<JL_OPENEX_T>(getSymbol("JLINKARM_OpenEx"));
        pJLINK_Close = reinterpret_cast<JL_CLOSE_T>(getSymbol("JLINKARM_Close"));
        pJLINK_JTAG_StoreRaw = reinterpret_cast<JL_JTAG_STORERAW_T>(getSymbol("JLINKARM_JTAG_StoreRaw"));
        pJLINK_JTAG_StoreGetRaw = reinterpret_cast<JL_JTAG_STOREGETRAW_T>(getSymbol("JLINKARM_JTAG_StoreGetRaw"));
        pJLINK_JTAG_SyncBits = reinterpret_cast<JL_JTAG_SYNCBITS_T>(getSymbol("JLINKARM_JTAG_SyncBits"));
        pJLINK_SetSpeed = reinterpret_cast<JL_SETSPEED_T>(getSymbol("JLINKARM_SetSpeed"));

        if (!pJLINK_OpenEx || !pJLINK_JTAG_StoreGetRaw) {
            std::cerr << "[JLink] Error: Missing symbols in DLL\n";
            unloadLibrary();
            return false;
        }

        return true;
    }

    void JLinkAdapter::unloadLibrary() {
        if (libHandle) {
#if defined(_WIN32)
            FreeLibrary(libHandle);
#else
            dlclose(libHandle);
#endif
            libHandle = nullptr;
        }
    }

    void* JLinkAdapter::getSymbol(const char* name) {
#if defined(_WIN32)
        return reinterpret_cast<void*>(GetProcAddress(libHandle, name));
#else
        return dlsym(libHandle, name);
#endif
    }

    bool JLinkAdapter::open() {
        if (connected) return true;

        if (!loadLibrary()) {
            std::cerr << "[JLink] Error: Could not load " << JLINK_LIB_NAME << "\n";
            return false;
        }

        const char* err = pJLINK_OpenEx(nullptr, 0);
        if (err) {
            std::cerr << "[JLink] OpenEx failed: " << err << "\n";
            return false;
        }

        if (pJLINK_SetSpeed) pJLINK_SetSpeed(1000);

        connected = true;
        std::cout << "[JLink] Connected via " << JLINK_LIB_NAME << "\n";
        return true;
    }

    void JLinkAdapter::close() {
        if (connected && pJLINK_Close) {
            pJLINK_Close();
        }
        unloadLibrary();
        connected = false;
    }

    bool JLinkAdapter::isConnected() const {
        return connected;
    }

    bool JLinkAdapter::setClockSpeed(uint32_t speedHz) {
        if (!connected || !pJLINK_SetSpeed) return false;
        pJLINK_SetSpeed(speedHz / 1000);
        currentSpeed = speedHz;
        return true;
    }

    std::string JLinkAdapter::getInfo() const {
        return connected ? "J-Link Connected (Dynamic Load)" : "J-Link Disconnected";
    }

    bool JLinkAdapter::shiftData(const std::vector<uint8_t>& tdi,
        std::vector<uint8_t>& tdo,
        size_t numBits,
        bool exitShift)
    {
        if (!connected) return false;

        size_t numBytes = (numBits + 7) / 8;
        tdo.resize(numBytes);

        std::vector<uint8_t> tms(numBytes, 0);

        if (exitShift && numBits > 0) {
            size_t lastBitIdx = numBits - 1;
            tms[lastBitIdx / 8] |= (1 << (lastBitIdx % 8));
        }

        int res = pJLINK_JTAG_StoreGetRaw(tdi.data(), tdo.data(), tms.data(), (uint32_t)numBits);

        if (pJLINK_JTAG_SyncBits) pJLINK_JTAG_SyncBits();

        return (res == 0);
    }

    bool JLinkAdapter::writeTMS(const std::vector<bool>& tmsSequence) {
        if (!connected) return false;

        size_t numBits = tmsSequence.size();
        size_t numBytes = (numBits + 7) / 8;

        std::vector<uint8_t> tmsBytes(numBytes, 0);
        std::vector<uint8_t> tdiBytes(numBytes, 0);

        for (size_t i = 0; i < numBits; ++i) {
            if (tmsSequence[i]) {
                tmsBytes[i / 8] |= (1 << (i % 8));
            }
        }

        int res = pJLINK_JTAG_StoreRaw(tdiBytes.data(), tmsBytes.data(), (uint32_t)numBits);

        if (pJLINK_JTAG_SyncBits) pJLINK_JTAG_SyncBits();

        return (res == 0);
    }

    bool JLinkAdapter::resetTAP() {
        return writeTMS({ 1, 1, 1, 1, 1 });
    }

    // ========== MÉTODOS DE ALTO NIVEL (transaccionales) ==========

    bool JLinkAdapter::scanIR(uint8_t irLength, const std::vector<uint8_t>& dataIn,
                              std::vector<uint8_t>& dataOut) {
        if (!connected) return false;

        std::cout << "[JLink] scanIR() - irLength: " << (int)irLength << "\n";

        // Navegar a Shift-IR: TMS sequence = 1,1,0,0 desde Run-Test/Idle
        std::vector<bool> toShiftIR = {true, true, false, false};
        if (!writeTMS(toShiftIR)) {
            std::cerr << "[JLink] ERROR: Failed to navigate to Shift-IR\n";
            return false;
        }

        // Shift IR data
        size_t byteCount = (irLength + 7) / 8;
        dataOut.resize(byteCount);
        if (!shiftData(dataIn, dataOut, irLength, true)) { // exitShift=true → Exit1-IR
            std::cerr << "[JLink] ERROR: Failed to shift IR data\n";
            return false;
        }

        // Navegar a Run-Test/Idle: TMS sequence = 1,0 desde Exit1-IR
        std::vector<bool> toIdle = {true, false};
        if (!writeTMS(toIdle)) {
            std::cerr << "[JLink] ERROR: Failed to return to Idle\n";
            return false;
        }

        std::cout << "[JLink] scanIR() - SUCCESS\n";
        return true;
    }

    bool JLinkAdapter::scanDR(size_t drLength, const std::vector<uint8_t>& dataIn,
                              std::vector<uint8_t>& dataOut) {
        if (!connected) return false;

        std::cout << "[JLink] scanDR() - drLength: " << drLength << "\n";

        // Navegar a Shift-DR: TMS sequence = 1,0,0 desde Run-Test/Idle
        std::vector<bool> toShiftDR = {true, false, false};
        if (!writeTMS(toShiftDR)) {
            std::cerr << "[JLink] ERROR: Failed to navigate to Shift-DR\n";
            return false;
        }

        // Shift DR data
        size_t byteCount = (drLength + 7) / 8;
        dataOut.resize(byteCount);
        if (!shiftData(dataIn, dataOut, drLength, true)) { // exitShift=true → Exit1-DR
            std::cerr << "[JLink] ERROR: Failed to shift DR data\n";
            return false;
        }

        // Navegar a Run-Test/Idle: TMS sequence = 1,0 desde Exit1-DR
        std::vector<bool> toIdle = {true, false};
        if (!writeTMS(toIdle)) {
            std::cerr << "[JLink] ERROR: Failed to return to Idle\n";
            return false;
        }

        std::cout << "[JLink] scanDR() - SUCCESS\n";
        return true;
    }

    uint32_t JLinkAdapter::readIDCODE() {
        if (!connected) return 0;

        std::cout << "[JLink] readIDCODE()\n";

        // Reset TAP (IDCODE se carga automáticamente en DR)
        if (!resetTAP()) {
            std::cerr << "[JLink] ERROR: Failed to reset TAP\n";
            return 0;
        }

        // Navegar a Shift-DR: TMS = 0,1,0,0 desde Test-Logic-Reset
        // (Reset→Idle→Select-DR→Capture-DR→Shift-DR)
        std::vector<bool> toShiftDR = {false, true, false, false};
        if (!writeTMS(toShiftDR)) {
            std::cerr << "[JLink] ERROR: Failed to navigate to Shift-DR\n";
            return 0;
        }

        // Leer 32 bits de IDCODE
        std::vector<uint8_t> dummy(4, 0);
        std::vector<uint8_t> idcodeBytes(4);
        if (!shiftData(dummy, idcodeBytes, 32, true)) {
            std::cerr << "[JLink] ERROR: Failed to read IDCODE\n";
            return 0;
        }

        // Convertir bytes → uint32_t (little-endian)
        uint32_t idcode = idcodeBytes[0] |
                         (idcodeBytes[1] << 8) |
                         (idcodeBytes[2] << 16) |
                         (idcodeBytes[3] << 24);

        std::cout << "[JLink] readIDCODE() - SUCCESS: 0x" << std::hex << idcode << std::dec << "\n";
        return idcode;
    }

} // namespace JTAG