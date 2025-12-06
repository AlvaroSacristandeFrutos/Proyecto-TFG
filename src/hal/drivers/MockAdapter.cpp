#include "MockAdapter.h"
#include <iostream>
#include <cstring>
#include <cmath>

namespace JTAG {

    MockAdapter::~MockAdapter() {
        close();
    }

    bool MockAdapter::open() {
        connected = true;
        simulationCounter = 0;
        std::cout << "[Mock] Simulator Started\n";
        return true;
    }

    void MockAdapter::close() {
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
            tdo[i] = (simulationCounter + i) ^ 0xAA;
        }

        return true;
    }

    bool MockAdapter::writeTMS(const std::vector<bool>& tmsSequence) {
        // Simulamos éxito instantáneo
        return true;
    }

    bool MockAdapter::resetTAP() {
        return true;
    }

} // namespace JTAG