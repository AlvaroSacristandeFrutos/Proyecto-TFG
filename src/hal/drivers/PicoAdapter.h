#pragma once

#include "../IJTAGAdapter.h"
#include <vector>
#include <string>

#if defined(_WIN32)
#include <windows.h>
#endif

namespace JTAG {

/**
 * @brief Raspberry Pi Pico JTAG Adapter
 *
 * Delega toda la comunicación en JLink_x64.dll (nuestra DLL personalizada),
 * usando exactamente el mismo patrón que JLinkAdapter / BoundaryScanner.
 * Así cualquier software con soporte JLink funciona sin cambios de código.
 */
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

    bool scanIR(uint8_t irLength, const std::vector<uint8_t>& dataIn,
                std::vector<uint8_t>& dataOut) override;
    bool scanDR(size_t drLength, const std::vector<uint8_t>& dataIn,
                std::vector<uint8_t>& dataOut) override;
    uint32_t readIDCODE() override;

    // Métodos de configuración
    std::string getName()       const override { return "Raspberry Pi Pico Probe"; }
    uint32_t    getClockSpeed() const override { return clockSpeed; }
    bool        setClockSpeed(uint32_t speedHz) override;
    std::string getInfo()       const override { return "JTAG via JLink_x64.dll"; }

    // Detección estática (misma API que antes, sin QSerialPort)
    static bool        isDeviceConnected();
    static std::string findPicoPort();

private:
    bool     connected  = false;
    uint32_t clockSpeed = 1000000;

    // Handle de la DLL y punteros a funciones
#if defined(_WIN32)
    HMODULE libHandle = nullptr;
#else
    void* libHandle = nullptr;
#endif

    typedef const char* (*JL_OPENEX_T)      (const char*, void*);
    typedef void        (*JL_CLOSE_T)       (void);
    typedef int         (*JL_STORERAW_T)    (const unsigned char*, const unsigned char*, unsigned int);
    typedef int         (*JL_STOREGETRAW_T) (const unsigned char*, unsigned char*, const unsigned char*, unsigned int);
    typedef void        (*JL_SYNCBITS_T)    (void);
    typedef void        (*JL_SETSPEED_T)    (unsigned int);
    typedef void        (*JL_RESET_T)       (void);

    JL_OPENEX_T      pOpenEx      = nullptr;
    JL_CLOSE_T       pClose       = nullptr;
    JL_STORERAW_T    pStoreRaw    = nullptr;
    JL_STOREGETRAW_T pStoreGetRaw = nullptr;
    JL_SYNCBITS_T    pSyncBits    = nullptr;
    JL_SETSPEED_T    pSetSpeed    = nullptr;
    JL_RESET_T       pReset       = nullptr;

    bool loadLibrary();
    void unloadLibrary();
};

} // namespace JTAG
