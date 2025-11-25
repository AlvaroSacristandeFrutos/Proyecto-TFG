#ifndef DEVICE_MODEL_H
#define DEVICE_MODEL_H

#include <string>
#include <vector>
#include <map>
#include <unordered_map>
#include <cstdint>
#include "BSDLParser.h" // Necesitamos los tipos del parser para cargar los datos

// ==========================================
// ESTRUCTURA 1: DATOS GLOBALES (La Ficha del Chip)
// ==========================================
struct ChipInfo {
    std::string deviceName;         // Ej: "STM32F407"
    std::string packageName;        // Ej: "LQFP100"

    uint32_t idCode = 0;

    // Parámetros JTAG críticos
    int boundaryLength = 0;         // Longitud total de la cadena de escaneo (BSR)
    int instructionLength = 0;      // Longitud del registro de instrucción (IR)
    std::string instructionCapture; // Patrón de bits de captura (útil para debug de conexión)

    // Diccionario de Opcodes (Ej: "EXTEST" -> "00000")
    // Usamos map normal aquí porque son pocos elementos y queremos orden al imprimir
    std::map<std::string, std::string> opcodes;

    // Estado seguro de arranque (Safe State) de toda la cadena
    std::vector<bool> bsrSafeState;

    std::string pinTCK;
    std::string pinTMS;
    std::string pinTDI;
    std::string pinTDO;
    std::string pinTRST;
};

// ==========================================
// ESTRUCTURA 2: DATOS POR PIN (La Info Detallada)
// ==========================================
enum class PinType { IO, POWER, GROUND, ANALOG, UNKNOWN };

struct PinInfo {
    // Identificación
    std::string logicalName;   // Nombre funcional (Ej: "PA5", "D0")
    std::string physicalPin;   // Coordenada física/Bola (Ej: "34", "K12")
    PinType type = PinType::UNKNOWN;

    // Mapeo JTAG (Los índices en la cadena de bits)
    // Si es -1, significa que este pin no tiene esa capacidad.
    int inputCell = -1;        // Bit donde leemos el valor externo
    int outputCell = -1;       // Bit donde escribimos el valor a sacar
    int controlCell = -1;      // Bit que habilita/deshabilita la salida (Tri-state)

    // Lógica de Control
    bool activeLow = false;    // true: se activa con '0', false: se activa con '1'
    std::string safeValue;     // Valor seguro ("0", "1", "Z") definido por el fabricante
};

// ==========================================
// CLASE PRINCIPAL (El Contenedor)
// ==========================================
class DeviceModel {
private:
    // 1. Instancia única de los datos globales
    ChipInfo info;

    // 2. El "Diccionario" maestro de pines
    // Clave: Nombre Lógico (string) -> Valor: Estructura PinInfo
    std::unordered_map<std::string, PinInfo> pinMap;

    // (Opcional) Índice inverso para buscar por pin físico rápido
    std::unordered_map<std::string, PinInfo*> physicalMap;

    // Estado actual de la simulación (el tren de bits)
    std::vector<bool> currentBitstream;

public:
    DeviceModel();

    // Carga los datos desde la estructura cruda del Parser a nuestras estructuras limpias
    void loadFromBSDL(const BSDLData& data);

    // --- ACCESO DE LECTURA ---
    const ChipInfo& getInfo() const { return info; }

    const PinInfo* getPin(const std::string& logicalName) const;
    const PinInfo* getPinByPhysical(const std::string& physicalName) const;

    // --- ACCESO DE ESCRITURA (Simulación) ---
    void setPinState(const std::string& logicalName, bool level);

    // Devuelve el estado actual de la cadena para enviarlo al JTAG
    const std::vector<bool>& getBitstream() const { return currentBitstream; }

    void printSummary() const;
};

#endif // DEVICE_MODEL_H