#pragma once

#include "../IJTAGAdapter.h"
#include <vector>
#include <string>
#include <optional>
#include <chrono>
#include <cstdint>

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

        // Métodos estáticos para detección
        static bool isLibraryAvailable();
        static bool isDeviceConnected();  // Detecta dispositivo USB físico

        // --- USB DEVICE ENUMERATION ---
        struct JLinkDeviceInfo {
            uint32_t serialNumber;
            std::string productName;
            std::string firmwareVersion;
            bool isUSB;
        };

        static std::vector<JLinkDeviceInfo> enumerateJLinkDevices();
        void setTargetSerialNumber(uint32_t serial);

    private:
        bool connected = false;
        DLL_HANDLE libHandle = nullptr;
        uint32_t currentSpeed = 1000000;
        uint32_t targetSerialNumber = 0;  // 0 = first available device

        // --- PUNTEROS A FUNCIONES DE LA DLL ---
        typedef const char* (JLINK_CALL_CONV* JL_OPENEX_T)(const char* pfLog, void*);
        typedef void        (JLINK_CALL_CONV* JL_CLOSE_T)(void);
        typedef int         (JLINK_CALL_CONV* JL_JTAG_STORERAW_T)(const uint8_t* pTDI, const uint8_t* pTMS, uint32_t NumBits);
        typedef int         (JLINK_CALL_CONV* JL_JTAG_STOREGETRAW_T)(const uint8_t* pTDI, uint8_t* pTDO, const uint8_t* pTMS, uint32_t NumBits);
        typedef void        (JLINK_CALL_CONV* JL_JTAG_SYNCBITS_T)(void);
        typedef void        (JLINK_CALL_CONV* JL_SETSPEED_T)(uint32_t Speed);
        typedef int         (JLINK_CALL_CONV* JL_EMU_SELECTBYUSBSN_T)(uint32_t SerialNo);

        JL_OPENEX_T              pJLINK_OpenEx = nullptr;
        JL_CLOSE_T               pJLINK_Close = nullptr;
        JL_JTAG_STORERAW_T       pJLINK_JTAG_StoreRaw = nullptr;
        JL_JTAG_STOREGETRAW_T    pJLINK_JTAG_StoreGetRaw = nullptr;
        JL_JTAG_SYNCBITS_T       pJLINK_JTAG_SyncBits = nullptr;
        JL_SETSPEED_T            pJLINK_SetSpeed = nullptr;
        JL_EMU_SELECTBYUSBSN_T   pJLINK_EMU_SelectByUSBSN = nullptr;

        bool loadLibrary();
        void unloadLibrary();
        void* getSymbol(const char* name);

        // --- DLL SEARCH WITH CACHING ---
        struct DLLCache {
            std::string path;
            std::chrono::system_clock::time_point timestamp;
            bool isValid() const;
        };

        static std::optional<DLLCache> s_dllCache;

        // Helper para búsqueda recursiva de DLL
        static std::string findJLinkDLL();
        static std::string searchRecursive(const std::string& basePath, int maxDepth, int timeoutMs);
        static void saveCacheToFile(const std::string& file, const std::string& path);
        static DLLCache loadCacheFromFile(const std::string& file);
    };

} // namespace JTAG