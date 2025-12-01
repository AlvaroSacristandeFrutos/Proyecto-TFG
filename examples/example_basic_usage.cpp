/**
 * @file example_basic_usage.cpp
 * @brief Basic usage example of the refactored JTAG Boundary Scanner
 *
 * This example demonstrates the complete workflow:
 * 1. Connect to a JTAG adapter
 * 2. Detect the target device
 * 3. Load device model (BSDL)
 * 4. Initialize boundary scan
 * 5. Control pins
 * 6. Read pins
 *
 * Compile with:
 *   g++ -std=c++17 -I../src example_basic_usage.cpp \
 *       ../src/core/BoundaryScanEngine.cpp \
 *       ../src/core/ScanController.cpp \
 *       ../src/bsdl/DeviceModel.cpp \
 *       ../src/hal/MockAdapter.cpp \
 *       -o boundary_scanner_demo
 */

#include "../src/core/ScanController.h"
#include <iostream>
#include <iomanip>

int main() {
    std::cout << "=== JTAG Boundary Scanner - Demo Application ===\n\n";

    try {
        // ====================================================================
        // 1. CREATE CONTROLLER
        // ====================================================================
        std::cout << "[1/6] Creating ScanController...\n";
        auto controller = std::make_unique<JTAG::ScanController>();

        // ====================================================================
        // 2. CONNECT ADAPTER
        // ====================================================================
        std::cout << "[2/6] Connecting to JTAG adapter (MockAdapter)...\n";
        if (!controller->connectAdapter(JTAG::AdapterType::MOCK, 1000000)) {
            std::cerr << "ERROR: Failed to connect adapter\n";
            return 1;
        }
        std::cout << "  Adapter: " << controller->getAdapterInfo() << "\n\n";

        // ====================================================================
        // 3. DETECT DEVICE
        // ====================================================================
        std::cout << "[3/6] Detecting JTAG device...\n";
        uint32_t idcode = controller->detectDevice();
        if (idcode == 0) {
            std::cerr << "ERROR: No device detected\n";
            return 1;
        }

        std::cout << "  IDCODE: 0x" << std::hex << std::setfill('0') << std::setw(8)
                  << idcode << std::dec << "\n";
        std::cout << "  Manufacturer: 0x" << std::hex << ((idcode >> 1) & 0x7FF) << std::dec << "\n";
        std::cout << "  Part Number: 0x" << std::hex << ((idcode >> 12) & 0xFFFF) << std::dec << "\n";
        std::cout << "  Version: " << ((idcode >> 28) & 0xF) << "\n\n";

        // ====================================================================
        // 4. LOAD DEVICE MODEL (BSDL)
        // ====================================================================
        std::cout << "[4/6] Loading device model (BSDL stub)...\n";
        if (!controller->loadBSDL("stm32f407vg.bsdl")) {
            std::cerr << "ERROR: Failed to load BSDL\n";
            return 1;
        }
        std::cout << "  Device: " << controller->getDeviceName() << "\n\n";

        // ====================================================================
        // 5. INITIALIZE BOUNDARY SCAN
        // ====================================================================
        std::cout << "[5/6] Initializing Boundary Scan...\n";
        if (!controller->initialize()) {
            std::cerr << "ERROR: Failed to initialize\n";
            return 1;
        }
        std::cout << "  System ready!\n\n";

        // ====================================================================
        // 6. CONTROL PINS (HIGH-LEVEL API)
        // ====================================================================
        std::cout << "[6/6] Controlling pins...\n\n";

        // Get list of available pins
        auto pins = controller->getPinList();
        std::cout << "Available pins (" << pins.size() << " total):\n";
        for (size_t i = 0; i < std::min(pins.size(), size_t(10)); i++) {
            std::cout << "  - " << pins[i] << "\n";
        }
        if (pins.size() > 10) {
            std::cout << "  ... and " << (pins.size() - 10) << " more\n";
        }
        std::cout << "\n";

        // Configure some pins
        std::cout << "Setting pins:\n";
        controller->setPin("PA0", JTAG::PinLevel::HIGH);
        std::cout << "  PA0 = HIGH\n";

        controller->setPin("PA1", JTAG::PinLevel::LOW);
        std::cout << "  PA1 = LOW\n";

        controller->setPin("PA2", JTAG::PinLevel::HIGH);
        std::cout << "  PA2 = HIGH\n";

        controller->setPin("PA3", JTAG::PinLevel::LOW);
        std::cout << "  PA3 = LOW\n";

        // Apply changes to hardware
        std::cout << "\nApplying changes to hardware...\n";
        if (!controller->applyChanges()) {
            std::cerr << "ERROR: Failed to apply changes\n";
            return 1;
        }
        std::cout << "  Changes applied successfully!\n\n";

        // Read pins back
        std::cout << "Reading pins back from hardware...\n";
        if (!controller->samplePins()) {
            std::cerr << "ERROR: Failed to sample pins\n";
            return 1;
        }

        std::vector<std::string> pinsToRead = {"PA0", "PA1", "PA2", "PA3"};
        for (const auto& pinName : pinsToRead) {
            auto level = controller->getPin(pinName);
            if (level) {
                std::cout << "  " << pinName << " = "
                          << (*level == JTAG::PinLevel::HIGH ? "HIGH" : "LOW")
                          << "\n";
            }
        }

        std::cout << "\n";

        // ====================================================================
        // 7. BULK PIN CONFIGURATION
        // ====================================================================
        std::cout << "Bulk pin configuration example:\n";
        std::map<std::string, JTAG::PinLevel> bulkConfig = {
            {"PB0", JTAG::PinLevel::HIGH},
            {"PB1", JTAG::PinLevel::HIGH},
            {"PB2", JTAG::PinLevel::LOW},
            {"PB3", JTAG::PinLevel::HIGH}
        };

        controller->setPins(bulkConfig);
        controller->applyChanges();
        std::cout << "  Configured " << bulkConfig.size() << " pins in one call\n\n";

        // ====================================================================
        // 8. RUN TEST CYCLES
        // ====================================================================
        std::cout << "Running 10 test cycles in Run-Test/Idle...\n";
        controller->runTest(10);
        std::cout << "  Test cycles completed\n\n";

        // ====================================================================
        // SUCCESS
        // ====================================================================
        std::cout << "=== Demo completed successfully! ===\n";

    } catch (const std::exception& e) {
        std::cerr << "EXCEPTION: " << e.what() << "\n";
        return 1;
    }

    return 0;
}
