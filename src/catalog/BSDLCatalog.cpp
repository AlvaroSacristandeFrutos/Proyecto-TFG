#include "BSDLCatalog.h"
#include "../parser/BSDLParser.h"
#include <filesystem>
#include <iostream>
#include <iomanip>

namespace fs = std::filesystem;

namespace JTAG {

bool BSDLCatalog::scanDirectory(const std::string& directory) {
    idcodeMap.clear();

    if (!fs::exists(directory) || !fs::is_directory(directory)) {
        std::cerr << "BSDLCatalog: Directory not found: " << directory << std::endl;
        return false;
    }

    std::cout << "BSDLCatalog: Scanning directory: " << directory << std::endl;

    for (const auto& entry : fs::directory_iterator(directory)) {
        if (!entry.is_regular_file()) continue;

        std::string ext = entry.path().extension().string();
        if (ext != ".bsd" && ext != ".bsdl") continue;

        std::string filePath = entry.path().string();
        std::cout << "BSDLCatalog: Found file: " << entry.path().filename() << std::endl;

        auto idcode = extractIDCODE(filePath);
        if (idcode.has_value()) {
            idcodeMap[idcode.value()] = filePath;
            std::cout << "BSDLCatalog: Indexed IDCODE 0x"
                      << std::hex << idcode.value() << std::dec
                      << " -> " << entry.path().filename() << std::endl;
        } else {
            std::cout << "BSDLCatalog: Failed to extract IDCODE from "
                      << entry.path().filename() << std::endl;
        }
    }

    std::cout << "BSDLCatalog: Total devices indexed: " << idcodeMap.size() << std::endl;
    return !idcodeMap.empty();
}

std::optional<uint32_t> BSDLCatalog::extractIDCODE(const std::string& bsdlPath) {
    BSDLParser parser;
    if (!parser.parse(bsdlPath)) {
        return std::nullopt;
    }

    uint32_t idcode = parser.getData().idCode;
    if (idcode == 0 || idcode == 0xFFFFFFFF) {
        return std::nullopt;
    }

    return idcode;
}

std::optional<std::string> BSDLCatalog::findByIDCODE(uint32_t idcode) const {
    std::cout << "BSDLCatalog::findByIDCODE() searching for 0x"
              << std::hex << std::setfill('0') << std::setw(8) << idcode
              << std::dec << std::endl;

    std::cout << "BSDLCatalog has " << idcodeMap.size() << " entries:" << std::endl;
    for (const auto& [id, path] : idcodeMap) {
        std::cout << "  - 0x" << std::hex << std::setfill('0') << std::setw(8)
                  << id << std::dec << " -> " << path << std::endl;
    }

    auto it = idcodeMap.find(idcode);
    if (it != idcodeMap.end()) {
        std::cout << "BSDLCatalog: MATCH FOUND!" << std::endl;
        return it->second;
    }

    std::cout << "BSDLCatalog: NO MATCH FOUND!" << std::endl;
    return std::nullopt;
}

} // namespace JTAG
