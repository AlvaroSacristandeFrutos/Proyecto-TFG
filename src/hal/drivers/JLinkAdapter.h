#pragma once

#include "../IJTAGAdapter.h"
#include <vector>
#include <string>

// --- CONFIGURACI�N DE PLATAFORMA ---
#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
typedef HMODULE DLL_HANDLE;
#define JLINK_CALL_CONV __stdcall
#else
#include <dlfcn.h>
typedef void* DLL_HANDLE;
#define JLINK_CALL_CONV
#endif
// -----------------------------------

namespace JTAG {

    class JLinkAdapter : public IJTAGAdapter {
    public:
        JLinkAdapter();
        ~JLinkAdapter() override;

        // --- Implementaci�n IJTAGAdapter ---
        bool open() override;
        void close() override;
        bool isConnected() const override;

        bool shiftData(const std::vector<uint8_t>& tdi,
            std::vector<uint8_t>& tdo,
            size_t numBits,
            bool exitShift = true) override;

        bool writeTMS(const std::vector<bool>& tmsSequence) override;
        bool resetTAP() override;

        // Métodos de alto nivel (transaccionales)
        bool scanIR(uint8_t irLength, const std::vector<uint8_t>& dataIn,
                    std::vector<uint8_t>& dataOut) override;
        bool scanDR(size_t drLength, const std::vector<uint8_t>& dataIn,
                    std::vector<uint8_t>& dataOut) override;
        uint32_t readIDCODE() override;

        // Info
        std::string getName() const override { return "SEGGER J-Link"; }
        uint32_t getClockSpeed() const override { return currentSpeed; }
        bool setClockSpeed(uint32_t speedHz) override;
        std::string getInfo() const override;

        // M�todo est�tico para detecci�n
        static bool isLibraryAvailable();

    private:
        bool connected = false;
        DLL_HANDLE libHandle = nullptr;
        uint32_t currentSpeed = 1000000;

        // --- PUNTEROS A FUNCIONES DE LA DLL ---
        typedef const char* (JLINK_CALL_CONV* JL_OPENEX_T)(const char* pfLog, void*);
        typedef void        (JLINK_CALL_CONV* JL_CLOSE_T)(void);
        typedef int         (JLINK_CALL_CONV* JL_JTAG_STORERAW_T)(const uint8_t* pTDI, const uint8_t* pTMS, uint32_t NumBits);
        typedef int         (JLINK_CALL_CONV* JL_JTAG_STOREGETRAW_T)(const uint8_t* pTDI, uint8_t* pTDO, const uint8_t* pTMS, uint32_t NumBits);
        typedef void        (JLINK_CALL_CONV* JL_JTAG_SYNCBITS_T)(void);
        typedef void        (JLINK_CALL_CONV* JL_SETSPEED_T)(uint32_t Speed);

        JL_OPENEX_T           pJLINK_OpenEx = nullptr;
        JL_CLOSE_T            pJLINK_Close = nullptr;
        JL_JTAG_STORERAW_T    pJLINK_JTAG_StoreRaw = nullptr;
        JL_JTAG_STOREGETRAW_T pJLINK_JTAG_StoreGetRaw = nullptr;
        JL_JTAG_SYNCBITS_T    pJLINK_JTAG_SyncBits = nullptr;
        JL_SETSPEED_T         pJLINK_SetSpeed = nullptr;

        bool loadLibrary();
        void unloadLibrary();
        void* getSymbol(const char* name);

        // Helper para búsqueda recursiva de DLL
        static std::string findJLinkDLL();
    };

} // namespace JTAG