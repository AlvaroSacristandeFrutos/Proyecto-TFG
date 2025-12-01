#pragma once

// --- INCLUDES OBLIGATORIOS PARA C++ ---
#include <string>       // Corrige: "string no es miembro de std"
#include <vector>       // Corrige: vectores
#include <map>          // Corrige: mapas
#include <optional>     // Corrige: optional
#include <cstdint>      // Corrige: uint32_t
// --------------------------------------

#include "../parser/BSDLParser.h" // Necesario para BSDLData

namespace JTAG {

    struct PinInfo {
        std::string name;
        int outputCell = -1;
        int inputCell = -1;
        int controlCell = -1;
    };

    class DeviceModel {
    public:
        DeviceModel();
        ~DeviceModel() = default;

        void loadFromData(const BSDLData& data);

        std::string getDeviceName() const { return deviceName; }
        uint32_t getIDCODE() const { return idcode; }
        size_t getIRLength() const { return irLength; }
        size_t getBSRLength() const { return bsrLength; }
        size_t getPinCount() const { return pins.size(); }

        std::optional<PinInfo> getPinInfo(const std::string& pinName) const;
        std::vector<std::string> getPinNames() const;
        const std::vector<PinInfo>& getAllPins() const { return pins; }

        uint32_t getInstruction(const std::string& instructionName) const;
        const std::map<std::string, uint32_t>& getAllInstructions() const { return instructions; }

    private:
        std::string deviceName;
        uint32_t idcode = 0;
        size_t irLength = 0;
        size_t bsrLength = 0;

        std::vector<PinInfo> pins;
        std::map<std::string, uint32_t> instructions;
    };

}