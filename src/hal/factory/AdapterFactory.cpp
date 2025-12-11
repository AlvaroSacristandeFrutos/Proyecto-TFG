#include "AdapterFactory.h"
#include "../drivers/MockAdapter.h"
#include "../drivers/PicoAdapter.h"
#include "../drivers/JLinkAdapter.h"
#include <memory>
#include <stdexcept>
#include <algorithm>
#include <cctype>

namespace JTAG {

    std::unique_ptr<IJTAGAdapter> AdapterFactory::create(AdapterType type) {
        switch (type) {
        case AdapterType::MOCK:
            return std::make_unique<MockAdapter>();

        case AdapterType::PICO:
            return std::make_unique<PicoAdapter>();

        case AdapterType::JLINK:
            return std::make_unique<JLinkAdapter>();

        case AdapterType::FT2232H:
            throw std::runtime_error("FT2232HAdapter no implementado aun");

        default:
            throw std::runtime_error("Tipo de adaptador desconocido");
        }
    }

    std::unique_ptr<IJTAGAdapter> AdapterFactory::createFromString(const std::string& typeName) {
        return create(stringToType(typeName));
    }

    std::string AdapterFactory::typeToString(AdapterType type) {
        switch (type) {
        case AdapterType::MOCK:    return "MOCK";
        case AdapterType::PICO:    return "PICO";
        case AdapterType::JLINK:   return "JLINK";
        case AdapterType::FT2232H: return "FT2232H";
        default:                   return "UNKNOWN";
        }
    }

    AdapterType AdapterFactory::stringToType(const std::string& typeName) {
        std::string upper = typeName;
        std::transform(upper.begin(), upper.end(), upper.begin(),
            [](unsigned char c) { return std::toupper(c); });

        if (upper == "MOCK")    return AdapterType::MOCK;
        if (upper == "PICO")    return AdapterType::PICO;
        if (upper == "JLINK")   return AdapterType::JLINK;
        if (upper == "FT2232H") return AdapterType::FT2232H;

        throw std::runtime_error("Tipo de adaptador desconocido: " + typeName);
    }

    bool AdapterFactory::isSupported(AdapterType type) {
        switch (type) {
        case AdapterType::MOCK:
        case AdapterType::PICO:
        case AdapterType::JLINK:
            return true;
        default:
            return false;
        }
    }

    std::vector<AdapterType> AdapterFactory::getSupportedAdapters() {
        std::vector<AdapterType> allTypes = {
            AdapterType::MOCK, AdapterType::PICO, AdapterType::JLINK, AdapterType::FT2232H
        };
        std::vector<AdapterType> supported;
        for (auto type : allTypes) {
            if (isSupported(type)) supported.push_back(type);
        }
        return supported;
    }

    std::vector<AdapterDescriptor> AdapterFactory::getAvailableAdapters() {
        // Lista estática - SIEMPRE muestra todos los adaptadores
        // La verificación de conexión física se hace en open()
        return {
            { AdapterType::MOCK,  "Mock Adapter",       "Loopback Simulation" },
            { AdapterType::JLINK, "SEGGER J-Link",      "JTAG/SWD Debugger" },
            { AdapterType::PICO,  "Raspberry Pi Pico",  "USB Serial JTAG" }
        };
    }

} // namespace JTAG