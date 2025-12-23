#include "AdapterFactory.h"
#include "../drivers/MockAdapter.h"
#include "../drivers/PicoAdapter.h"
#include "../drivers/JLinkAdapter.h"
#include <memory>
#include <stdexcept>
#include <algorithm>
#include <cctype>
#include <iostream>

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

    std::unique_ptr<IJTAGAdapter> AdapterFactory::create(AdapterType type, const std::string& deviceID) {
        switch (type) {
        case AdapterType::MOCK:
            return std::make_unique<MockAdapter>();

        case AdapterType::PICO:
            return std::make_unique<PicoAdapter>();

        case AdapterType::JLINK: {
            auto jlink = std::make_unique<JLinkAdapter>();

            // Extract serial number from deviceID (format: "JLINK_12345678")
            if (!deviceID.empty() && deviceID.find("JLINK_") == 0) {
                try {
                    uint32_t serial = std::stoul(deviceID.substr(6));
                    jlink->setTargetSerialNumber(serial);
                }
                catch (const std::exception& e) {
                    // Agregar e.what() al log
                    std::cerr << "[Factory] Warning: Failed to parse J-Link serial (" << e.what() << ") from deviceID: " << deviceID << "\n";
                }
            }

            return jlink;
        }

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
        std::vector<AdapterDescriptor> availableAdapters;

        std::cout << "[Factory] Detecting available JTAG adapters...\n";

        // 1. MOCK: Only in Debug build
#ifdef _DEBUG
        std::cout << "[Factory] Adding Mock adapter (Debug build only)\n";
        availableAdapters.push_back({
            AdapterType::MOCK,
            "Mock Adapter",
            "Debug Only",
            "MOCK_DEBUG"
        });
#endif

        // 2. PICO: USB Detection (already implemented)
        if (PicoAdapter::isDeviceConnected()) {
            std::string picoPort = PicoAdapter::findPicoPort();
            std::cout << "[Factory] Found Raspberry Pi Pico on port: " << picoPort << "\n";
            availableAdapters.push_back({
                AdapterType::PICO,
                "Raspberry Pi Pico",
                picoPort.empty() ? "USB Device" : picoPort,
                "PICO_" + picoPort
            });
        }

        // 3. JLINK: USB Enumeration
        auto jlinkDevices = JLinkAdapter::enumerateJLinkDevices();
        std::cout << "[Factory] Found " << jlinkDevices.size() << " J-Link device(s)\n";

        for (const auto& device : jlinkDevices) {
            availableAdapters.push_back({
                AdapterType::JLINK,
                "SEGGER " + device.productName,
                "S/N: " + std::to_string(device.serialNumber),
                "JLINK_" + std::to_string(device.serialNumber)
            });
        }

        std::cout << "[Factory] Total available adapters: " << availableAdapters.size() << "\n";
        return availableAdapters;
    }

} // namespace JTAG