#pragma once

#include "../IJTAGAdapter.h"
#include <cstdint>
#include <vector>
#include <string>

namespace JTAG {

/**
 * @brief Adaptador JTAG simulado para testing y desarrollo
 *
 * Características:
 * - Modo loopback: TDO = TDI (lo que envías es lo que recibes)
 * - Sin hardware real (simulación en memoria)
 * - Logging de operaciones en consola
 * - Estadísticas de uso (operaciones, bits)
 *
 * Uso:
 *   auto adapter = std::make_unique<MockAdapter>();
 *   adapter->open();
 *   // ... operaciones JTAG ...
 *   adapter->close();
 *
 * @note Ideal para desarrollo y tests unitarios sin hardware
 */
class MockAdapter : public IJTAGAdapter {
public:
    MockAdapter() = default;
    ~MockAdapter() override;

    // ========================================================================
    // OPERACIONES JTAG (de IJTAGAdapter)
    // ========================================================================

    /**
     * @brief Desplaza datos por TDI/TDO en modo loopback
     *
     * Simula operación SHIFT con TDO = TDI (loopback).
     * Registra operación y actualiza estadísticas.
     *
     * @param tdi Datos a enviar
     * @param tdo Datos recibidos (será copia de tdi)
     * @param numBits Número de bits a desplazar
     * @param exitShift Si true, simula salida de estado SHIFT
     * @return true si operación exitosa
     */
    bool shiftData(
        const std::vector<uint8_t>& tdi,
        std::vector<uint8_t>& tdo,
        size_t numBits,
        bool exitShift = true) override;

    /**
     * @brief Simula envío de secuencia TMS
     *
     * Imprime secuencia en consola para debugging.
     *
     * @param tmsSequence Secuencia de valores TMS
     * @return true siempre (simulación)
     */
    bool writeTMS(const std::vector<bool>& tmsSequence) override;

    /**
     * @brief Simula reset del TAP controller
     *
     * @return true siempre (simulación)
     */
    bool resetTAP() override;

    /**
     * @brief Simula pulsos de reloj
     *
     * @param numClocks Número de pulsos a generar
     * @return true siempre (simulación)
     */

    // ========================================================================
    // GESTIÓN DE CONEXIÓN (de IJTAGAdapter)
    // ========================================================================

    /**
     * @brief Abre conexión simulada
     *
     * Inicializa estado interno y estadísticas.
     *
     * @return true si conexión exitosa
     */
    bool open() override;

    /**
     * @brief Cierra conexión y muestra estadísticas
     */
    void close() override;

    /**
     * @brief Verifica si está conectado
     *
     * @return true si open() fue llamado
     */
    bool isConnected() const override;

    // ========================================================================
    // INFORMACIÓN Y CONFIGURACIÓN (de IJTAGAdapter)
    // ========================================================================

    /**
     * @brief Obtiene nombre del adaptador
     *
     * @return "Mock JTAG Adapter"
     */
    std::string getName() const override;

    /**
     * @brief Obtiene velocidad de reloj configurada
     *
     * @return Frecuencia en Hz (valor simulado)
     */
    uint32_t getClockSpeed() const override;

    /**
     * @brief Establece velocidad de reloj (simulada)
     *
     * @param speedHz Frecuencia deseada en Hz
     * @return true siempre (simulación)
     */
    bool setClockSpeed(uint32_t speedHz) override;

    /**
     * @brief Obtiene información del adaptador
     *
     * @return String con descripción del Mock Adapter
     */
    std::string getInfo() const override;

    // ========================================================================
    // MÉTODOS ADICIONALES PARA TESTING
    // ========================================================================

    /**
     * @brief Obtiene número total de operaciones realizadas
     *
     * @return Contador de operaciones desde open()
     */
    size_t getTotalOperations() const;

    /**
     * @brief Obtiene número total de bits desplazados
     *
     * @return Contador de bits desde open()
     */
    size_t getTotalBitsShifted() const;

    /**
     * @brief Resetea estadísticas a cero
     */
    void resetStatistics();

private:
    bool connected = false;           ///< Estado de conexión
    uint32_t clockSpeed = 1000000;    ///< Velocidad de reloj simulada (1 MHz)
    size_t totalOperations = 0;       ///< Contador de operaciones
    size_t totalBitsShifted = 0;      ///< Contador de bits desplazados
};

} // namespace JTAG
