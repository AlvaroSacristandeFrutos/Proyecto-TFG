#include "JLinkAdapter.h"
#include <iostream>
#include <vector>
#include <cstring>
#include <filesystem>
#include <algorithm>

namespace fs = std::filesystem;

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

    // Función helper para buscar recursivamente la DLL de J-Link
    std::string JLinkAdapter::findJLinkDLL() {
#if defined(_WIN32)
        // 1. PRIORIDAD ALTA: Buscar recursivamente en directorio SEGGER
        std::string seggerPath = "C:\\Program Files\\SEGGER";
        std::cout << "[JLink] Searching recursively in: " << seggerPath << "\n";

        std::vector<std::string> foundDLLs;

        try {
            if (fs::exists(seggerPath) && fs::is_directory(seggerPath)) {
                for (const auto& entry : fs::recursive_directory_iterator(seggerPath, fs::directory_options::skip_permission_denied)) {
                    if (entry.is_regular_file()) {
                        std::string filename = entry.path().filename().string();
                        if (filename == "JLink_x64.dll" || filename == "JLinkARM.dll") {
                            foundDLLs.push_back(entry.path().string());
                        }
                    }
                }
            }
        } catch (const std::exception& e) {
            std::cerr << "[JLink] Error searching SEGGER directory: " << e.what() << "\n";
        }

        // Priorizar: 1) JLink_x64.dll, 2) Versiones más recientes (V890 > V884)
        if (!foundDLLs.empty()) {
            std::cout << "[JLink] Found " << foundDLLs.size() << " DLLs:\n";
            for (const auto& dll : foundDLLs) {
                std::cout << "[JLink]   - " << dll << "\n";
            }

            // Ordenar: primero x64, luego versiones más altas
            std::sort(foundDLLs.begin(), foundDLLs.end(), [](const std::string& a, const std::string& b) {
                bool aIs64 = a.find("JLink_x64.dll") != std::string::npos;
                bool bIs64 = b.find("JLink_x64.dll") != std::string::npos;
                if (aIs64 != bIs64) return aIs64; // x64 primero
                return a > b; // Versión más alta (alfabéticamente: V890 > V884)
            });

            std::string bestDLL = foundDLLs[0];
            std::cout << "[JLink] Selected DLL: " << bestDLL << "\n";
            return bestDLL;
        }

        // 2. FALLBACK: Buscar en otras ubicaciones comunes
        std::cout << "[JLink] DLL not found in SEGGER, trying fallback locations...\n";
        const char* fallbackPaths[] = {
            "C:\\Program Files (x86)\\SEGGER\\JLink\\JLink_x64.dll",
            "C:\\Program Files\\SEGGER\\JLink\\JLink_x64.dll"
        };

        for (const char* path : fallbackPaths) {
            if (fs::exists(path)) {
                std::cout << "[JLink] Found DLL at fallback: " << path << "\n";
                return std::string(path);
            }
        }

        std::cout << "[JLink] DLL not found in any location\n";
#endif
        return ""; // No encontrada
    }

    // Método estático para verificar si la DLL existe en el sistema
    bool JLinkAdapter::isLibraryAvailable() {
#if defined(_WIN32)
        // Intentar cargar desde PATH primero
        HMODULE h = LoadLibraryA(JLINK_LIB_NAME);
        if (h) {
            FreeLibrary(h);
            std::cout << "[JLink] DLL found in PATH: " << JLINK_LIB_NAME << "\n";
            return true;
        }

        // Si no está en PATH, buscar recursivamente
        std::string dllPath = findJLinkDLL();
        if (!dllPath.empty()) {
            h = LoadLibraryA(dllPath.c_str());
            if (h) {
                FreeLibrary(h);
                return true;
            }
        }

        std::cout << "[JLink] DLL not available\n";
#else
        void* h = dlopen(JLINK_LIB_NAME, RTLD_LAZY);
        if (h) {
            dlclose(h);
            return true;
        }
#endif
        return false;
    }

    bool JLinkAdapter::isDeviceConnected() {
        // TEMPORAL: Retornar true si la DLL está disponible
        // Esto es equivalente al comportamiento anterior para testing
        // El usuario puede verificar manualmente si su J-Link está conectado

        bool dllAvailable = isLibraryAvailable();
        std::cout << "[JLink] isDeviceConnected: DLL available = " << (dllAvailable ? "YES" : "NO") << "\n";

        if (!dllAvailable) {
            return false;
        }

        std::cout << "[JLink] isDeviceConnected: Returning TRUE (DLL detection mode for testing)\n";
        std::cout << "[JLink] NOTE: Actual USB device detection would require JLINKARM_EMU_GetList()\n";

        return true;  // TEMPORAL: Para que aparezca en la lista si la DLL existe

#if 0  // Código anterior con EMU_GetList (comentado temporalmente)
        // Paso 1: Verificar que DLL existe (prerequisito)
        if (!isLibraryAvailable()) {
            std::cout << "[JLink] isDeviceConnected: DLL not available\n";
            return false;  // Sin DLL no podemos comunicarnos con J-Link
        }
        std::cout << "[JLink] isDeviceConnected: DLL found, checking for USB devices...\n";

#if defined(_WIN32)
        // Paso 2: Cargar DLL temporalmente
        HMODULE tempHandle = LoadLibraryA(JLINK_LIB_NAME);
        if (!tempHandle) {
            std::string dllPath = findJLinkDLL();
            if (!dllPath.empty()) {
                tempHandle = LoadLibraryA(dllPath.c_str());
            }
        }

        if (!tempHandle) {
            return false;
        }

        // Paso 3: Obtener puntero a JLINKARM_EMU_GetList
        typedef unsigned int (JLINK_CALL_CONV* JL_EMU_GETLIST_T)(
            unsigned int, void*, unsigned int);

        JL_EMU_GETLIST_T pJLINK_EMU_GetList =
            reinterpret_cast<JL_EMU_GETLIST_T>(
                GetProcAddress(tempHandle, "JLINKARM_EMU_GetList"));

        if (!pJLINK_EMU_GetList) {
            std::cout << "[JLink] ERROR: JLINKARM_EMU_GetList function not found in DLL\n";
            FreeLibrary(tempHandle);
            return false;  // Función no disponible en esta versión de DLL
        }
        std::cout << "[JLink] JLINKARM_EMU_GetList function found, calling it...\n";

        // Paso 4: Consultar dispositivos conectados
        unsigned int numDevices = pJLINK_EMU_GetList(
            0,      // InterfaceMask: 0 = auto-detect (USB + IP)
            NULL,   // pEmuInfo: NULL para solo contar
            0       // MaxInfos: 0 para solo contar
        );

        std::cout << "[JLink] isDeviceConnected: Found " << numDevices << " J-Link device(s)\n";

        // Paso 5: Limpiar y retornar
        FreeLibrary(tempHandle);
        return numDevices > 0;  // true si hay al menos un J-Link conectado
#else
        // Linux/macOS: Similar implementación con dlopen
        void* tempHandle = dlopen(JLINK_LIB_NAME, RTLD_LAZY);
        if (!tempHandle) {
            return false;
        }

        typedef unsigned int (*JL_EMU_GETLIST_T)(unsigned int, void*, unsigned int);
        JL_EMU_GETLIST_T pJLINK_EMU_GetList =
            reinterpret_cast<JL_EMU_GETLIST_T>(dlsym(tempHandle, "JLINKARM_EMU_GetList"));

        if (!pJLINK_EMU_GetList) {
            dlclose(tempHandle);
            return false;
        }

        unsigned int numDevices = pJLINK_EMU_GetList(0, NULL, 0);
        dlclose(tempHandle);
        return numDevices > 0;
#endif
#endif  // Fin del #if 0
    }

    bool JLinkAdapter::loadLibrary() {
        if (libHandle) return true;

#if defined(_WIN32)
        // Intentar cargar desde PATH primero
        libHandle = LoadLibraryA(JLINK_LIB_NAME);

        if (!libHandle) {
            // Si no está en PATH, buscar recursivamente
            std::string dllPath = findJLinkDLL();
            if (!dllPath.empty()) {
                libHandle = LoadLibraryA(dllPath.c_str());
                if (libHandle) {
                    std::cout << "[JLink] Loaded DLL from: " << dllPath << "\n";
                }
            }
        } else {
            std::cout << "[JLink] Loaded DLL from PATH: " << JLINK_LIB_NAME << "\n";
        }
#else
        libHandle = dlopen(JLINK_LIB_NAME, RTLD_NOW);
#endif

        if (!libHandle) {
            std::cerr << "[JLink] Error: Could not load DLL from any location\n";
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

        // std::cout << "[JLink] scanIR() - irLength: " << (int)irLength << "\n";

        // --- CORRECCIÓN: NAVEGACIÓN SEGURA (SIN RESET) ---
        // En lugar de resetear (que mata el EXTEST), usamos un '0' inicial.
        // Si estamos en RESET, '0' nos lleva a IDLE.
        // Si estamos en IDLE, '0' nos mantiene en IDLE.
        // Secuencia: Idle(0) -> SelectDR(1) -> SelectIR(1) -> CaptureIR(0) -> ShiftIR(0)

        std::vector<bool> toShiftIR = { false, true, true, false, false };

        if (!writeTMS(toShiftIR)) {
            std::cerr << "[JLink] ERROR: Failed to navigate to Shift-IR\n";
            return false;
        }

        // Shift IR data
        size_t byteCount = (irLength + 7) / 8;
        dataOut.resize(byteCount);
        if (!shiftData(dataIn, dataOut, irLength, true)) {
            std::cerr << "[JLink] ERROR: Failed to shift IR data\n";
            return false;
        }

        // Volver a Idle
        std::vector<bool> toIdle = { true, false };
        if (!writeTMS(toIdle)) return false;

        return true;
    }

    bool JLinkAdapter::scanDR(size_t drLength, const std::vector<uint8_t>& dataIn,
        std::vector<uint8_t>& dataOut) {
        if (!connected) return false;

        // std::cout << "[JLink] scanDR() - drLength: " << drLength << "\n";

        // 1. Navegar a Shift-DR con "Safety Zero"
        //    Idle(0) -> Select-DR(1) -> Capture-DR(0) -> Shift-DR(0)
        //    El paso por Capture-DR (el primer 0) es el que TOMA LA FOTO de los pines.
        std::vector<bool> toShiftDR = { false, true, false, false };

        if (!writeTMS(toShiftDR)) {
            std::cerr << "[JLink] ERROR: Failed to navigate to Shift-DR\n";
            return false;
        }

        // 2. Desplazar datos (Shift DR)
        size_t byteCount = (drLength + 7) / 8;
        dataOut.resize(byteCount);
        // exitShift=true nos lleva a Exit1-DR
        if (!shiftData(dataIn, dataOut, drLength, true)) {
            std::cerr << "[JLink] ERROR: Failed to shift DR data\n";
            return false;
        }

        // 3. Volver a Idle
        //    Exit1-DR -> Update-DR(1) -> Run-Test/Idle(0)
        std::vector<bool> toIdle = { true, false };
        if (!writeTMS(toIdle)) return false;

        // Debug opcional: ver datos brutos para confirmar que no es IDCODE (AB 7F...)
        /*
        std::cout << "[JLink] DR Data: ";
        for(auto b : dataOut) printf("%02X ", b);
        std::cout << "\n";
        */

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