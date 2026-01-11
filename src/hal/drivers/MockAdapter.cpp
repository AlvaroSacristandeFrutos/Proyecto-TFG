#include "MockAdapter.h"
#include <iostream>
#include <iomanip>
#include <cstring>
#include <cmath>
#include <thread>
#include <chrono>

namespace JTAG {

    MockAdapter::~MockAdapter() {
        close();
    }

    bool MockAdapter::open() {
        connected = true;
        simulationCounter = 0;
        std::cout << "[MockAdapter] Simulator Started - IDCODE: 0x12345678\n";
        return true;
    }

    void MockAdapter::close() {
        if (connected) {
            std::cout << "[MockAdapter] Simulator Closed\n";
        }
        connected = false;
    }

    bool MockAdapter::isConnected() const {
        return connected;
    }

    bool MockAdapter::setClockSpeed(uint32_t speedHz) {
        clockSpeed = speedHz;
        return true;
    }

    bool MockAdapter::shiftData(const std::vector<uint8_t>& tdi,
        std::vector<uint8_t>& tdo,
        size_t numBits,
        bool exitShift)
    {
        if (!connected) return false;

        // Simular latencia USB realista
        std::this_thread::sleep_for(std::chrono::milliseconds(5 + numBits / 100));

        size_t numBytes = (numBits + 7) / 8;
        tdo.resize(numBytes);

        // --- LÓGICA DE SIMULACIÓN ---

        // CASO 1: Lectura de IDCODE (Normalmente 32 bits)
        if (numBits == 32) {
            // Devolvemos un ID ficticio: 0x12345678
            // JTAG es LSB first, así que lo guardamos en Little Endian
            if (numBytes >= 4) {
                tdo[0] = 0x78;
                tdo[1] = 0x56;
                tdo[2] = 0x34;
                tdo[3] = 0x12;
            }
            return true;
        }

        // CASO 2: Boundary Scan (Lectura de pines)
        // Generamos un patrón de bits que cambia cada vez que llamamos a la función
        // para que en la GUI parezca que hay actividad real.

        simulationCounter++; // Avanzamos el "reloj" de simulación

        for (size_t i = 0; i < numBytes; i++) {
            // Genera un patrón tipo "contador binario" mezclado con "walking ones"
            // Esto hará que los LEDs de la GUI parpadeen de forma interesante
            tdo[i] = static_cast<uint8_t>((simulationCounter + i) ^ 0xAA);
        }

        return true;
    }

    bool MockAdapter::writeTMS(const std::vector<bool>& tmsSequence) {
        if (!connected) return false;

        // Simular latencia de comando
        std::this_thread::sleep_for(std::chrono::milliseconds(2));

        return true;
    }

    bool MockAdapter::resetTAP() {
        return true;
    }

    // ========== MÉTODOS DE ALTO NIVEL (transaccionales) ==========

    bool MockAdapter::scanIR(uint8_t irLength, const std::vector<uint8_t>& dataIn,
                             std::vector<uint8_t>& dataOut) {
        if (!connected) return false;

        // Simular latencia realista (navegación TAP + shift)
        std::this_thread::sleep_for(std::chrono::milliseconds(10 + irLength / 10));

        // Simular operación: copiar dataIn → dataOut (loopback simple)
        size_t byteCount = (irLength + 7) / 8;
        dataOut.resize(byteCount);
        if (!dataIn.empty()) {
            std::copy(dataIn.begin(), dataIn.begin() + std::min(dataIn.size(), dataOut.size()),
                      dataOut.begin());
        }

        std::cout << "[Mock] scanIR() - irLength: " << (int)irLength << " bits\n";
        return true;
    }

    bool MockAdapter::scanDR(size_t drLength, const std::vector<uint8_t>& dataIn,
                             std::vector<uint8_t>& dataOut) {
        if (!connected) return false;

        // Simular latencia realista (navegación TAP + shift)
        std::this_thread::sleep_for(std::chrono::milliseconds(10 + drLength / 100));

        // === DATOS CAMBIANTES PARA VISUALIZACIÓN ===
        size_t byteCount = (drLength + 7) / 8;
        dataOut.resize(byteCount);

        simulationCounter++; // Incrementar contador de simulación

        // DEBUG: Mostrar qué recibimos
        std::cout << "[Mock] scanDR() - drLength: " << drLength << " bits (" << byteCount << " bytes)"
                  << ", dataIn.size: " << dataIn.size()
                  << ", counter: " << (int)simulationCounter << "\n";

        // MODO SIMULACIÓN: SIEMPRE generar datos dinámicos
        // MockAdapter es un simulador para testing, no necesitamos distinguir SAMPLE/EXTEST
        // Siempre generamos datos cambiantes para visualización
        std::cout << "[Mock]   → Generating dynamic simulation data\n";

        for (size_t i = 0; i < byteCount; ++i) {
            // Patrón 1: Contador binario en los primeros bytes
            if (i < 4) {
                dataOut[i] = (simulationCounter + (i * 37)) & 0xFF;
            }
            // Patrón 2: Walking ones en bytes intermedios
            else if (i < byteCount / 2) {
                uint8_t walkingBit = (1 << ((simulationCounter / 4) % 8));
                dataOut[i] = walkingBit;
            }
            // Patrón 3: Patrón de alternancia en bytes finales
            else {
                dataOut[i] = ((simulationCounter / 2) % 2) ? 0xFF : 0x00;
            }
        }

        // DEBUG: Mostrar primeros bytes generados
        std::cout << "[Mock]   → Generated: ";
        for (size_t i = 0; i < std::min(byteCount, size_t(4)); ++i) {
            std::cout << "0x" << std::hex << std::setw(2) << std::setfill('0')
                      << (int)dataOut[i] << " ";
        }
        std::cout << std::dec << "\n";

        return true;
    }

    uint32_t MockAdapter::readIDCODE() {
        if (!connected) return 0;

        // Simular latencia de operación completa
        std::this_thread::sleep_for(std::chrono::milliseconds(5));

        // Devolver IDCODE fijo del MockAdapter
        std::cout << "[Mock] readIDCODE() - returning 0x12345678\n";
        return 0x12345678;
    }

} // namespace JTAG