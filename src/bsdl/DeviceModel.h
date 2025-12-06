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
        std::string port;          // Puerto/señal del BSDL
        std::string type;          // Tipo: INPUT, OUTPUT, INOUT, etc.
        std::string pinNumber;     // Número físico del pin en el package (alfanumérico: "A1", "B2", etc.)
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
        std::string getPackageInfo() const { return packageInfo; }
        size_t getPinCount() const { return pins.size(); }

        std::optional<PinInfo> getPinInfo(const std::string& pinName) const;
        std::vector<std::string> getPinNames() const;
        const std::vector<PinInfo>& getAllPins() const { return pins; }

        // Métodos para información adicional de pines
        std::string getPinPort(const std::string& pinName) const;
        std::string getPinType(const std::string& pinName) const;
        std::string getPinNumber(const std::string& pinName) const;

        uint32_t getInstruction(const std::string& instructionName) const;
        const std::map<std::string, uint32_t>& getAllInstructions() const { return instructions; }

    private:
        std::string deviceName;
        uint32_t idcode = 0;
        size_t irLength = 0;
        size_t bsrLength = 0;
        std::string packageInfo;

        std::vector<PinInfo> pins;
        std::map<std::string, uint32_t> instructions;
    };

}