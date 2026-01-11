#include "src/hal/factory/AdapterFactory.h"
#include "src/hal/drivers/JLinkAdapter.h"
#include <iostream>

int main() {
    std::cout << "=== TESTING ADAPTER DETECTION ===\n\n";

    // Test 1: J-Link Library Available
    std::cout << "Test 1: J-Link Library Available\n";
    bool libAvailable = JTAG::JLinkAdapter::isLibraryAvailable();
    std::cout << "  Result: " << (libAvailable ? "YES - DLL found" : "NO - DLL not found") << "\n\n";

    // Test 2: J-Link Device Connected (NEW)
    std::cout << "Test 2: J-Link Device Connected (Physical USB)\n";
    bool deviceConnected = JTAG::JLinkAdapter::isDeviceConnected();
    std::cout << "  Result: " << (deviceConnected ? "YES - USB device connected" : "NO - No USB device") << "\n\n";

    // Test 3: Get Available Adapters (Static List)
    std::cout << "Test 3: Get Available Adapters (Static List)\n";
    auto adapters = JTAG::AdapterFactory::getAvailableAdapters();
    std::cout << "  Found " << adapters.size() << " adapter(s):\n";
    for (const auto& adapter : adapters) {
        std::cout << "    - " << adapter.name << " (" << adapter.description << ")\n";
    }

    std::cout << "\n=== EXPECTED BEHAVIOR ===\n";
    std::cout << "- All adapters: Always shown in list (static)\n";
    std::cout << "- Connection check happens when open() is called\n";
    std::cout << "- J-Link: open() fails if no USB device\n";
    std::cout << "- Pico: open() fails if no Pico USB device\n";

    return 0;
}
