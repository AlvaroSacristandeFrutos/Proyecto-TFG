#include "JLinkAdapter.h"
#include <iostream>
#include <vector>
#include <cstring>
#include <filesystem>
#include <algorithm>
#include <fstream>
#include <chrono>

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

    // Static cache variable initialization
    std::optional<JLinkAdapter::DLLCache> JLinkAdapter::s_dllCache = std::nullopt;

    JLinkAdapter::JLinkAdapter() {}

    JLinkAdapter::~JLinkAdapter() {
        close();
    }

    // --- CACHE VALIDATION ---
    bool JLinkAdapter::DLLCache::isValid() const {
        auto now = std::chrono::system_clock::now();
        auto age = std::chrono::duration_cast<std::chrono::hours>(now - timestamp);
        return age.count() < 24;  // Cache válido por 24 horas
    }

    // --- SAVE CACHE TO FILE ---
    void JLinkAdapter::saveCacheToFile(const std::string& file, const std::string& path) {
        try {
            std::ofstream ofs(file);
            if (ofs.is_open()) {
                auto now = std::chrono::system_clock::now();
                auto timestamp = std::chrono::system_clock::to_time_t(now);
                ofs << path << "\n" << timestamp << "\n";
                ofs.close();
                std::cout << "[JLink] Cache saved to: " << file << "\n";
            }
        } catch (const std::exception& e) {
            std::cerr << "[JLink] Error saving cache: " << e.what() << "\n";
        }
    }

    // --- LOAD CACHE FROM FILE ---
    JLinkAdapter::DLLCache JLinkAdapter::loadCacheFromFile(const std::string& file) {
        DLLCache cache;
        try {
            std::ifstream ifs(file);
            if (ifs.is_open()) {
                std::string path;
                std::time_t timestamp;
                std::getline(ifs, path);
                ifs >> timestamp;
                ifs.close();

                cache.path = path;
                cache.timestamp = std::chrono::system_clock::from_time_t(timestamp);

                if (cache.isValid() && fs::exists(cache.path)) {
                    std::cout << "[JLink] Cache loaded from file: " << cache.path << "\n";
                    return cache;
                } else {
                    std::cout << "[JLink] Cache expired or path invalid\n";
                }
            }
        } catch (const std::exception& e) {
            std::cerr << "[JLink] Error loading cache: " << e.what() << "\n";
        }
        return DLLCache{};  // Return empty cache
    }

    // --- RECURSIVE SEARCH WITH BLACKLIST AND TIMEOUT ---
    std::string JLinkAdapter::searchRecursive(const std::string& basePath, int maxDepth, int timeoutMs) {
        auto startTime = std::chrono::steady_clock::now();

        // Blacklist de directorios a evitar
        std::vector<std::string> blacklist = {
            "Windows", "System32", "$Recycle.Bin", "node_modules",
            "ProgramData", "Users", "AppData"
        };

        try {
            if (!fs::exists(basePath) || !fs::is_directory(basePath)) {
                return "";
            }

            std::cout << "[JLink] Searching recursively in: " << basePath
                     << " (maxDepth=" << maxDepth << ", timeout=" << timeoutMs << "ms)\n";

            // Use error code instead of exceptions for better Unicode handling
            std::error_code ec;
            for (const auto& entry : fs::recursive_directory_iterator(
                basePath,
                fs::directory_options::skip_permission_denied,
                ec)) {

                // Check timeout
                auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                    std::chrono::steady_clock::now() - startTime);
                if (elapsed.count() > timeoutMs) {
                    std::cout << "[JLink] Search timeout after " << elapsed.count() << "ms\n";
                    return "";
                }

                // Check depth limit
                try {
                    auto relativePath = entry.path().lexically_relative(basePath);
                    int depth = std::distance(relativePath.begin(), relativePath.end());
                    if (depth > maxDepth) {
                        continue;
                    }
                } catch (...) {
                    // Skip entries with path issues
                    continue;
                }

                // Check blacklist
                bool isBlacklisted = false;
                std::string pathStr;
                try {
                    pathStr = entry.path().string();
                } catch (...) {
                    // Skip entries with encoding issues
                    continue;
                }

                for (const auto& blacklisted : blacklist) {
                    if (pathStr.find(blacklisted) != std::string::npos) {
                        isBlacklisted = true;
                        break;
                    }
                }
                if (isBlacklisted) continue;

                // Check if it's the DLL we're looking for
                try {
                    if (entry.is_regular_file()) {
                        std::string filename = entry.path().filename().string();
                        if (filename == "JLink_x64.dll" || filename == "JLinkARM.dll") {
                            std::cout << "[JLink] Found DLL: " << entry.path().string() << "\n";
                            return entry.path().string();
                        }
                    }
                } catch (...) {
                    // Skip entries with access/encoding issues
                    continue;
                }
            }

            // Check if the iterator itself had errors
            if (ec) {
                std::cerr << "[JLink] Iterator error: " << ec.message() << "\n";
            }
        } catch (const std::exception& e) {
            std::cerr << "[JLink] Error in recursive search: " << e.what() << "\n";
        }

        return "";
    }

    // Función helper para buscar recursivamente la DLL de J-Link con caché
    std::string JLinkAdapter::findJLinkDLL() {
#if defined(_WIN32)
        std::cout << "[JLink] Starting DLL search with caching strategy...\n";
        std::cout << "[JLink] Looking for: " << JLINK_LIB_NAME << "\n";

        // 1. Check memory cache
        if (s_dllCache.has_value() && s_dllCache->isValid() && fs::exists(s_dllCache->path)) {
            // Validate that the cached DLL can actually be loaded
            HMODULE testHandle = LoadLibraryA(s_dllCache->path.c_str());
            if (testHandle) {
                FreeLibrary(testHandle);
                std::cout << "[JLink] Using memory cache: " << s_dllCache->path << "\n";
                return s_dllCache->path;
            } else {
                std::cout << "[JLink] Memory cached DLL cannot be loaded, invalidating\n";
                s_dllCache.reset();
            }
        }

        // 2. Check disk cache
        std::string cacheFile = std::string(getenv("TEMP")) + "\\jlink_dll_cache.txt";
        DLLCache diskCache = loadCacheFromFile(cacheFile);
        if (!diskCache.path.empty()) {
            // Validate that the cached DLL can actually be loaded
            HMODULE testHandle = LoadLibraryA(diskCache.path.c_str());
            if (testHandle) {
                FreeLibrary(testHandle);
                std::cout << "[JLink] Disk cache validated and working\n";
                s_dllCache = diskCache;
                return diskCache.path;
            } else {
                std::cout << "[JLink] Cached DLL exists but cannot be loaded, invalidating cache\n";
                // Delete invalid cache file
                try {
                    fs::remove(cacheFile);
                } catch (...) {}
            }
        }

        // 3. Check project directory (local DLLs)
        std::cout << "[JLink] Checking project directory...\n";
        char exePath[MAX_PATH];
        if (GetModuleFileNameA(NULL, exePath, MAX_PATH)) {
            fs::path exeDir = fs::path(exePath).parent_path();

            // Check in executable directory
            std::string localDLL = (exeDir / JLINK_LIB_NAME).string();
            if (fs::exists(localDLL)) {
                HMODULE testHandle = LoadLibraryA(localDLL.c_str());
                if (testHandle) {
                    FreeLibrary(testHandle);
                    std::cout << "[JLink] Found in project directory: " << localDLL << "\n";
                    DLLCache newCache{localDLL, std::chrono::system_clock::now()};
                    s_dllCache = newCache;
                    saveCacheToFile(cacheFile, localDLL);
                    return localDLL;
                }
            }

            // Also check in project root (parent directory)
            std::string projectDLL = (exeDir.parent_path() / JLINK_LIB_NAME).string();
            if (fs::exists(projectDLL)) {
                HMODULE testHandle = LoadLibraryA(projectDLL.c_str());
                if (testHandle) {
                    FreeLibrary(testHandle);
                    std::cout << "[JLink] Found in project root: " << projectDLL << "\n";
                    DLLCache newCache{projectDLL, std::chrono::system_clock::now()};
                    s_dllCache = newCache;
                    saveCacheToFile(cacheFile, projectDLL);
                    return projectDLL;
                }
            }
        }

        // 4. Try LoadLibrary from PATH (fastest - no file search)
        std::cout << "[JLink] Trying LoadLibrary from PATH...\n";
        HMODULE testHandle = LoadLibraryA(JLINK_LIB_NAME);
        if (testHandle) {
            char pathBuf[MAX_PATH];
            if (GetModuleFileNameA(testHandle, pathBuf, MAX_PATH)) {
                std::string foundPath(pathBuf);
                FreeLibrary(testHandle);
                std::cout << "[JLink] Found in PATH: " << foundPath << "\n";

                // Save to cache
                DLLCache newCache{foundPath, std::chrono::system_clock::now()};
                s_dllCache = newCache;
                saveCacheToFile(cacheFile, foundPath);

                return foundPath;
            }
            FreeLibrary(testHandle);
        }

        // 5. Search SEGGER subdirectories (fast - no deep recursion, just 1 level)
        std::cout << "[JLink] Scanning SEGGER installation directories...\n";
        std::vector<std::string> seggerBasePaths = {
            "C:\\Program Files\\SEGGER",
            "C:\\Program Files (x86)\\SEGGER"
        };

        for (const auto& basePath : seggerBasePaths) {
            try {
                if (!fs::exists(basePath) || !fs::is_directory(basePath)) {
                    continue;
                }

                // Iterate through subdirectories (1 level deep only)
                for (const auto& entry : fs::directory_iterator(basePath)) {
                    if (!entry.is_directory()) continue;

                    // Check if DLL exists in this subdirectory
                    std::string dllPath = entry.path().string() + "\\" + JLINK_LIB_NAME;
                    if (fs::exists(dllPath)) {
                        std::cout << "[JLink] Found in: " << dllPath << "\n";

                        // Validate it can be loaded
                        HMODULE testHandle = LoadLibraryA(dllPath.c_str());
                        if (testHandle) {
                            FreeLibrary(testHandle);
                            std::cout << "[JLink] DLL validated successfully\n";
                            DLLCache newCache{dllPath, std::chrono::system_clock::now()};
                            s_dllCache = newCache;
                            saveCacheToFile(cacheFile, dllPath);
                            return dllPath;
                        } else {
                            std::cout << "[JLink] DLL found but cannot be loaded, skipping\n";
                        }
                    }
                }
            } catch (const std::exception& e) {
                std::cerr << "[JLink] Error scanning " << basePath << ": " << e.what() << "\n";
            }
        }

        // 6. Deep search in Program Files\SEGGER (fallback if DLL is in subdirectories)
        std::cout << "[JLink] Deep search in Program Files\\SEGGER (checking subdirectories)...\n";
        std::string result = searchRecursive("C:\\Program Files\\SEGGER", 10, 60000);
        if (!result.empty()) {
            DLLCache newCache{result, std::chrono::system_clock::now()};
            s_dllCache = newCache;
            saveCacheToFile(cacheFile, result);
            return result;
        }

        // 7. Deep search in Program Files (x86)\SEGGER (fallback)
        std::cout << "[JLink] Deep search in Program Files (x86)\\SEGGER (checking subdirectories)...\n";
        result = searchRecursive("C:\\Program Files (x86)\\SEGGER", 10, 60000);
        if (!result.empty()) {
            DLLCache newCache{result, std::chrono::system_clock::now()};
            s_dllCache = newCache;
            saveCacheToFile(cacheFile, result);
            return result;
        }

        std::cout << "[JLink] DLL not found in any location\n";
#endif
        return ""; // No encontrada
    }

    // --- USB DEVICE SELECTION ---
    void JLinkAdapter::setTargetSerialNumber(uint32_t serial) {
        targetSerialNumber = serial;
        std::cout << "[JLink] Target serial number set to: " << serial << "\n";
    }

    // --- USB DEVICE ENUMERATION ---
#if defined(_WIN32)
    // SEGGER J-Link EMU_INFO structure (from JLINKARM SDK)
    struct JLINKARM_EMU_INFO {
        uint32_t SerialNumber;
        uint32_t Connection;     // 0=USB, 1=IP
        uint32_t USBAddr;
        uint8_t  aIPAddr[16];
        int      Time;
        uint64_t Time_us;
        uint32_t HWVersion;
        uint8_t  abMACAddr[6];
        char     acProduct[32];
        char     acNickname[32];
        char     acFWString[112];
        char     aDummy[32];
    };
#endif

    std::vector<JLinkAdapter::JLinkDeviceInfo> JLinkAdapter::enumerateJLinkDevices() {
        std::vector<JLinkDeviceInfo> devices;

#if defined(_WIN32)
        std::cout << "[JLink] Enumerating USB devices...\n";

        // Step 1: Find DLL
        std::string dllPath = findJLinkDLL();
        if (dllPath.empty()) {
            // Try from PATH
            HMODULE tempHandle = LoadLibraryA(JLINK_LIB_NAME);
            if (!tempHandle) {
                std::cout << "[JLink] ERROR: DLL not available for enumeration\n";
                return devices;
            }

            // Get DLL path
            char pathBuf[MAX_PATH];
            if (GetModuleFileNameA(tempHandle, pathBuf, MAX_PATH)) {
                dllPath = std::string(pathBuf);
            }
            FreeLibrary(tempHandle);

            if (dllPath.empty()) {
                return devices;
            }
        }

        // Step 2: Load DLL temporarily
        HMODULE tempHandle = LoadLibraryA(dllPath.c_str());
        if (!tempHandle) {
            std::cout << "[JLink] ERROR: Failed to load DLL for enumeration\n";
            return devices;
        }

        // Step 3: Get JLINKARM_EMU_GetList function pointer
        typedef unsigned int (JLINK_CALL_CONV* JL_EMU_GETLIST_T)(
            unsigned int InterfaceMask,
            JLINKARM_EMU_INFO* pEmuInfo,
            unsigned int MaxInfos);

        JL_EMU_GETLIST_T pJLINK_EMU_GetList =
            reinterpret_cast<JL_EMU_GETLIST_T>(
                GetProcAddress(tempHandle, "JLINKARM_EMU_GetList"));

        if (!pJLINK_EMU_GetList) {
            std::cout << "[JLink] ERROR: JLINKARM_EMU_GetList not found in DLL\n";
            FreeLibrary(tempHandle);
            return devices;
        }

        // Step 4: First call - count devices
        unsigned int numDevices = pJLINK_EMU_GetList(
            1,          // InterfaceMask: 1 = USB only
            nullptr,    // pEmuInfo: NULL to count
            0           // MaxInfos: 0 to count
        );

        std::cout << "[JLink] Found " << numDevices << " J-Link device(s)\n";

        if (numDevices == 0) {
            FreeLibrary(tempHandle);
            return devices;
        }

        // Step 5: Second call - get device info
        std::vector<JLINKARM_EMU_INFO> emuInfo(numDevices);
        unsigned int retrieved = pJLINK_EMU_GetList(
            1,                  // InterfaceMask: 1 = USB only
            emuInfo.data(),     // pEmuInfo: buffer
            numDevices          // MaxInfos: buffer size
        );

        std::cout << "[JLink] Retrieved info for " << retrieved << " device(s)\n";

        // Step 6: Convert to JLinkDeviceInfo
        for (unsigned int i = 0; i < retrieved; ++i) {
            JLinkDeviceInfo info;
            info.serialNumber = emuInfo[i].SerialNumber;
            info.productName = std::string(emuInfo[i].acProduct);
            info.firmwareVersion = std::string(emuInfo[i].acFWString);
            info.isUSB = (emuInfo[i].Connection == 0);

            devices.push_back(info);

            std::cout << "[JLink]   Device " << i << ": "
                     << info.productName << " (S/N: " << info.serialNumber << ")"
                     << " FW: " << info.firmwareVersion << "\n";
        }

        // Step 7: Clean up
        FreeLibrary(tempHandle);
#endif

        return devices;
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
        pJLINK_EMU_SelectByUSBSN = reinterpret_cast<JL_EMU_SELECTBYUSBSN_T>(getSymbol("JLINKARM_EMU_SelectByUSBSN"));

        if (!pJLINK_OpenEx || !pJLINK_JTAG_StoreGetRaw) {
            std::cerr << "[JLink] Error: Missing symbols in DLL\n";
            unloadLibrary();
            return false;
        }

        // Select specific device if serial number is specified
        if (targetSerialNumber != 0 && pJLINK_EMU_SelectByUSBSN) {
            std::cout << "[JLink] Selecting device with serial: " << targetSerialNumber << "\n";
            int result = pJLINK_EMU_SelectByUSBSN(targetSerialNumber);
            if (result < 0) {
                std::cerr << "[JLink] Warning: Failed to select device by serial number\n";
            }
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

        if (pJLINK_SetSpeed) pJLINK_SetSpeed(12000);

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

        std::cout << "[JLink] scanDR() - drLength: " << drLength << "\n";

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