#include "MockAdapter.h"
#include <iostream>
#include <cstring>

namespace JTAG {

MockAdapter::~MockAdapter() {
    close();
}

bool MockAdapter::shiftData(
    const std::vector<uint8_t>& tdi,
    std::vector<uint8_t>& tdo,
    size_t numBits,
    bool exitShift)
{
    if (!connected) {
        std::cerr << "MockAdapter: Not connected\n";
        return false;
    }

    // Validar tamaño
    size_t numBytes = (numBits + 7) / 8;
    if (tdi.size() < numBytes) {
        std::cerr << "MockAdapter: TDI buffer too small\n";
        return false;
    }

    // Simular loopback: TDO = TDI
    tdo.resize(numBytes);
    std::memcpy(tdo.data(), tdi.data(), numBytes);

    // Estadísticas
    totalBitsShifted += numBits;
    totalOperations++;

    // Debug info
    if (numBits <= 64) {  // Solo para operaciones pequeñas
        std::cout << "MockAdapter::shiftData(" << numBits << " bits, exitShift="
                  << (exitShift ? "true" : "false") << ")\n";

        // Mostrar primeros bytes
        std::cout << "  TDI: ";
        for (size_t i = 0; i < std::min(numBytes, size_t(8)); i++) {
            printf("%02X ", tdi[i]);
        }
        if (numBytes > 8) std::cout << "...";
        std::cout << "\n";
    }

    return true;
}

bool MockAdapter::writeTMS(const std::vector<bool>& tmsSequence) {
    if (!connected) {
        std::cerr << "MockAdapter: Not connected\n";
        return false;
    }

    std::cout << "MockAdapter::writeTMS(" << tmsSequence.size() << " bits): ";
    for (bool tms : tmsSequence) {
        std::cout << (tms ? '1' : '0');
    }
    std::cout << "\n";

    totalOperations++;
    return true;
}

bool MockAdapter::resetTAP() {
    if (!connected) {
        std::cerr << "MockAdapter: Not connected\n";
        return false;
    }

    std::cout << "MockAdapter::resetTAP()\n";
    totalOperations++;
    return true;
}



bool MockAdapter::open() {
    if (connected) {
        std::cout << "MockAdapter: Already connected\n";
        return true;
    }

    std::cout << "MockAdapter::open() - Mock JTAG adapter initialized\n";
    connected = true;
    totalBitsShifted = 0;
    totalOperations = 0;
    return true;
}

void MockAdapter::close() {
    if (!connected) return;

    std::cout << "MockAdapter::close()\n";
    std::cout << "  Total operations: " << totalOperations << "\n";
    std::cout << "  Total bits shifted: " << totalBitsShifted << "\n";

    connected = false;
}

bool MockAdapter::isConnected() const {
    return connected;
}

std::string MockAdapter::getName() const {
    return "Mock JTAG Adapter";
}

uint32_t MockAdapter::getClockSpeed() const {
    return clockSpeed;
}

bool MockAdapter::setClockSpeed(uint32_t speedHz) {
    std::cout << "MockAdapter::setClockSpeed(" << speedHz << " Hz)\n";
    clockSpeed = speedHz;
    return true;
}

std::string MockAdapter::getInfo() const {
    return "Mock JTAG Adapter v1.0 (Loopback: TDO=TDI)";
}

// Métodos adicionales para testing
size_t MockAdapter::getTotalOperations() const {
    return totalOperations;
}

size_t MockAdapter::getTotalBitsShifted() const {
    return totalBitsShifted;
}

void MockAdapter::resetStatistics() {
    totalOperations = 0;
    totalBitsShifted = 0;
}

// Factory implementation (declarada en IJTAGAdapter.h)
std::unique_ptr<IJTAGAdapter> createAdapter(AdapterType type) {
    switch (type) {
        case AdapterType::MOCK:
            return std::make_unique<MockAdapter>();

        // TODO: Implementar otros adaptadores
        case AdapterType::PICO:
            throw std::runtime_error("PicoAdapter not implemented yet");
        case AdapterType::FT2232H:
            throw std::runtime_error("FT2232Adapter not implemented yet");
        case AdapterType::JLINK:
            throw std::runtime_error("JLinkAdapter not implemented yet");

        default:
            throw std::runtime_error("Unknown adapter type");
    }
}

} // namespace JTAG
