#include "PicoAdapter.h"
#include <iostream>

#if defined(_WIN32)
    #define JLINK_LIB_NAME "JLink_x64.dll"
#else
    #define JLINK_LIB_NAME "./libjlinkarm.so"
#endif

namespace JTAG {

// -------------------------------------------------------------------------
// Detección estática: carga la DLL y consulta el número de dispositivos
// -------------------------------------------------------------------------

bool PicoAdapter::isDeviceConnected() {
    /* Delega en GetList igual que JLinkAdapter::enumerateJLinkDevices().
     * La DLL usa caché interna (3 s) para no escanear el puerto dos veces
     * cuando JLinkAdapter también llama a GetList en el mismo ciclo. */
#if defined(_WIN32)
    HMODULE h = LoadLibraryA(JLINK_LIB_NAME);
    if (!h) return false;
    typedef unsigned int (__cdecl *FnGetList)(unsigned int, void*, unsigned int);
    auto fn = reinterpret_cast<FnGetList>(
                  GetProcAddress(h, "JLINKARM_EMU_GetList"));
    unsigned int n = fn ? fn(1u, nullptr, 0u) : 0u;
    FreeLibrary(h);
    return n > 0u;
#else
    return false;
#endif
}

std::string PicoAdapter::findPicoPort() {
    return isDeviceConnected() ? JLINK_LIB_NAME : "";
}

// -------------------------------------------------------------------------
// Constructor / Destructor
// -------------------------------------------------------------------------

PicoAdapter::PicoAdapter() {}

PicoAdapter::~PicoAdapter() {
    close();
}

// -------------------------------------------------------------------------
// Carga / descarga de la DLL
// -------------------------------------------------------------------------

bool PicoAdapter::loadLibrary() {
    if (libHandle) return true;

#if defined(_WIN32)
    libHandle = LoadLibraryA(JLINK_LIB_NAME);
#else
    libHandle = dlopen(JLINK_LIB_NAME, RTLD_NOW);
#endif
    if (!libHandle) {
        std::cerr << "[PicoAdapter] Cannot load " << JLINK_LIB_NAME << "\n";
        return false;
    }

#if defined(_WIN32)
    #define GETPROC(var, name) \
        var = reinterpret_cast<decltype(var)>(GetProcAddress(libHandle, name))
#else
    #define GETPROC(var, name) \
        var = reinterpret_cast<decltype(var)>(dlsym(libHandle, name))
#endif

    GETPROC(pOpenEx,      "JLINKARM_OpenEx");
    GETPROC(pClose,       "JLINKARM_Close");
    GETPROC(pStoreRaw,    "JLINKARM_JTAG_StoreRaw");
    GETPROC(pStoreGetRaw, "JLINKARM_JTAG_StoreGetRaw");
    GETPROC(pSyncBits,    "JLINKARM_JTAG_SyncBits");
    GETPROC(pSetSpeed,    "JLINKARM_SetSpeed");
    GETPROC(pReset,       "JLINKARM_Reset");

#undef GETPROC

    if (!pOpenEx || !pStoreGetRaw || !pSyncBits) {
        std::cerr << "[PicoAdapter] Missing symbols in " << JLINK_LIB_NAME << "\n";
        unloadLibrary();
        return false;
    }
    return true;
}

void PicoAdapter::unloadLibrary() {
    if (libHandle) {
#if defined(_WIN32)
        FreeLibrary(libHandle);
#else
        dlclose(libHandle);
#endif
        libHandle = nullptr;
    }
    pOpenEx = nullptr; pClose       = nullptr;
    pStoreRaw = nullptr; pStoreGetRaw = nullptr;
    pSyncBits = nullptr; pSetSpeed    = nullptr;
    pReset    = nullptr;
}

// -------------------------------------------------------------------------
// Gestión de conexión
// -------------------------------------------------------------------------

bool PicoAdapter::open() {
    if (connected) return true;
    if (!loadLibrary()) return false;

    const char* err = pOpenEx(nullptr, nullptr);
    if (err) {
        std::cerr << "[PicoAdapter] OpenEx: " << err << "\n";
        unloadLibrary();
        return false;
    }

    if (pSetSpeed) pSetSpeed(clockSpeed / 1000u);

    connected = true;
    std::cout << "[PicoAdapter] Connected via " << JLINK_LIB_NAME << "\n";
    return true;
}

void PicoAdapter::close() {
    if (connected && pClose) pClose();
    unloadLibrary();
    connected = false;
}

bool PicoAdapter::isConnected() const {
    return connected;
}

bool PicoAdapter::setClockSpeed(uint32_t speedHz) {
    clockSpeed = speedHz;
    if (connected && pSetSpeed) {
        pSetSpeed(speedHz / 1000u);
        return true;
    }
    return false;
}

// -------------------------------------------------------------------------
// Primitivas JTAG
// -------------------------------------------------------------------------

bool PicoAdapter::shiftData(const std::vector<uint8_t>& tdi,
                             std::vector<uint8_t>& tdo,
                             size_t numBits,
                             bool exitShift)
{
    if (!connected) return false;

    size_t numBytes = (numBits + 7u) / 8u;
    std::vector<uint8_t> tms(numBytes, 0u);
    std::vector<uint8_t> tdoRaw(numBytes, 0u);

    if (exitShift && numBits > 0u) {
        size_t lastBit = numBits - 1u;
        tms[lastBit / 8u] |= static_cast<uint8_t>(1u << (lastBit % 8u));
    }

    if (pStoreGetRaw)
        pStoreGetRaw(tdi.data(), tdoRaw.data(), tms.data(),
                     static_cast<unsigned int>(numBits));
    if (pSyncBits) pSyncBits();

    tdo = tdoRaw;
    return true;
}

bool PicoAdapter::writeTMS(const std::vector<bool>& tmsSequence) {
    if (!connected) return false;

    size_t numBits  = tmsSequence.size();
    size_t numBytes = (numBits + 7u) / 8u;
    std::vector<uint8_t> tms(numBytes, 0u);
    std::vector<uint8_t> tdi(numBytes, 0u);

    for (size_t i = 0u; i < numBits; ++i)
        if (tmsSequence[i])
            tms[i / 8u] |= static_cast<uint8_t>(1u << (i % 8u));

    if (pStoreRaw)
        pStoreRaw(tdi.data(), tms.data(), static_cast<unsigned int>(numBits));
    if (pSyncBits) pSyncBits();

    return true;
}

bool PicoAdapter::resetTAP() {
    if (!connected) return false;
    if (pReset) pReset();
    return true;
}

// -------------------------------------------------------------------------
// Métodos de alto nivel
// -------------------------------------------------------------------------

bool PicoAdapter::scanIR(uint8_t irLength,
                          const std::vector<uint8_t>& dataIn,
                          std::vector<uint8_t>& dataOut)
{
    if (!connected) return false;
    if (!writeTMS({false, true, true, false, false})) return false;
    if (!shiftData(dataIn, dataOut, irLength, true))  return false;
    return writeTMS({true, false});
}

bool PicoAdapter::scanDR(size_t drLength,
                          const std::vector<uint8_t>& dataIn,
                          std::vector<uint8_t>& dataOut)
{
    if (!connected) return false;
    if (!writeTMS({false, true, false, false})) return false;
    if (!shiftData(dataIn, dataOut, drLength, true)) return false;
    return writeTMS({true, false});
}

uint32_t PicoAdapter::readIDCODE() {
    if (!connected) return 0u;

    if (pReset) pReset();
    if (!writeTMS({false, true, false, false})) return 0u;

    std::vector<uint8_t> dummy(4u, 0u), idBytes(4u);
    if (!shiftData(dummy, idBytes, 32u, true)) return 0u;
    writeTMS({true, false});

    return static_cast<uint32_t>(idBytes[0])
         | (static_cast<uint32_t>(idBytes[1]) << 8u)
         | (static_cast<uint32_t>(idBytes[2]) << 16u)
         | (static_cast<uint32_t>(idBytes[3]) << 24u);
}

} // namespace JTAG
