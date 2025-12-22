#include "DeviceModel.h"
#include <algorithm>
#include <iostream>
#include <cctype>
#include <regex>

namespace JTAG {

    // Helper: Comparación alfanumérica para ordenar pines correctamente
    // A1 < A2 < A10 < B1 (en lugar de A1 < A10 < A2)
    static bool compareAlphanumeric(const std::string& a, const std::string& b) {
        std::regex numberPattern("(\\D*)(\\d*)");
        std::smatch matchA, matchB;

        auto itA = a.begin();
        auto itB = b.begin();

        while (itA != a.end() && itB != b.end()) {
            std::string remainA(itA, a.end());
            std::string remainB(itB, b.end());

            if (std::regex_search(remainA, matchA, numberPattern) &&
                std::regex_search(remainB, matchB, numberPattern)) {

                // Comparar parte alfabética
                if (matchA[1] != matchB[1]) {
                    return matchA[1] < matchB[1];
                }

                // Comparar parte numérica
                if (!matchA[2].str().empty() && !matchB[2].str().empty()) {
                    int numA = std::stoi(matchA[2]);
                    int numB = std::stoi(matchB[2]);
                    if (numA != numB) {
                        return numA < numB;
                    }
                }

                itA += matchA.length();
                itB += matchB.length();
            } else {
                break;
            }
        }

        return a < b; // Fallback alfabético
    }

    DeviceModel::DeviceModel() {
        // Constructor vacío. El modelo nace inválido hasta que se llama a loadFromData()
    }

    void DeviceModel::loadFromData(const BSDLData& data) {
        this->deviceName = data.entityName;
        this->idcode = data.idCode;
        this->bsrLength = data.boundaryLength;
        this->irLength = data.instructionLength;
        this->packageInfo = data.physicalPinMap;

        // 1. Cargar Instrucciones (igual que antes)
        this->instructions.clear();
        for (const auto& instr : data.instructions) {
            if (!instr.opcodes.empty()) {
                try {
                    // Manejo de 'X' en opcodes (don't care -> 0) para conversión segura
                    std::string cleanOpcode = instr.opcodes[0];
                    std::replace(cleanOpcode.begin(), cleanOpcode.end(), 'X', '0');
                    this->instructions[instr.name] = std::stoul(cleanOpcode, nullptr, 2);
                } catch (...) {}
            }
        }

        // 2. CREAR PINES DESDE LOS PUERTOS (Source of Truth)
        // Esto asegura que VCC, GND y pines LINKAGE existan en el modelo
        this->pins.clear();
        std::map<std::string, PinInfo> tempPinMap;

        for (const auto& port : data.ports) {
            PinInfo newPin;
            newPin.name = port.name;
            newPin.port = port.name;
            newPin.type = port.direction; // "in", "out", "inout", "linkage", "buffer"

            // Normalizar tipos para consistencia (lowercase)
            std::string typeUpper = newPin.type;
            std::transform(typeUpper.begin(), typeUpper.end(), typeUpper.begin(), ::toupper);

            if (typeUpper == "LINKAGE") newPin.type = "linkage";
            else if (typeUpper == "IN") newPin.type = "input";
            else if (typeUpper == "OUT" || typeUpper == "BUFFER") newPin.type = "output";
            else if (typeUpper == "INOUT") newPin.type = "inout";
            else newPin.type = "unknown";

            // Buscar Pin Físico (Mapping)
            auto mapIt = data.pinMaps.find(port.name);
            if (mapIt != data.pinMaps.end() && !mapIt->second.empty()) {
                newPin.pinNumber = mapIt->second[0];
            } else {
                newPin.pinNumber = "";  // Pin lógico sin mapeo físico
            }

            // Inicializar celdas como -1 (No conectadas al JTAG por defecto)
            newPin.inputCell = -1;
            newPin.outputCell = -1;
            newPin.controlCell = -1;

            tempPinMap[port.name] = newPin;
        }

        // 3. ENRIQUECER CON DATOS DE BOUNDARY SCAN
        // Ahora recorremos las celdas para asignar la lógica JTAG a los pines existentes
        for (const auto& cell : data.boundaryCells) {
            if (cell.portName == "*") continue; // Celdas internas/control puro

            // Buscar el pin correspondiente
            auto it = tempPinMap.find(cell.portName);
            if (it == tempPinMap.end()) {
                // Celda referencia un puerto que no vimos (raro pero posible)
                continue;
            }

            PinInfo& pin = it->second;

            // Asignar celdas según función
            switch (cell.function) {
                case CellFunction::INPUT:
                case CellFunction::CLOCK:
                    pin.inputCell = cell.cellNumber;
                    break;

                case CellFunction::OUTPUT2:
                case CellFunction::OUTPUT3:
                    pin.outputCell = cell.cellNumber;
                    if (cell.controlCell != -1) {
                        pin.controlCell = cell.controlCell;
                    }
                    break;

                case CellFunction::BIDIR:
                    // BIDIR normalmente tiene DOS celdas (input + output)
                    // Asignamos de forma incremental
                    if (pin.inputCell == -1) {
                        pin.inputCell = cell.cellNumber;
                    } else if (pin.outputCell == -1) {
                        pin.outputCell = cell.cellNumber;
                    }
                    if (cell.controlCell != -1) {
                        pin.controlCell = cell.controlCell;
                    }
                    break;

                case CellFunction::CONTROL:
                    // Las celdas de control a veces tienen nombre de puerto asociado
                    // para indicar qué grupo controlan, pero no son el pin en sí.
                    break;

                default:
                    break;
            }
        }

        // 4. Mover al vector final
        this->pins.reserve(tempPinMap.size());
        for (auto const& [name, info] : tempPinMap) {
            this->pins.push_back(info);
        }

        // 5. Ordenar pines por número físico (layout del chip)
        std::sort(this->pins.begin(), this->pins.end(), [](const PinInfo& a, const PinInfo& b) {
            // Si ambos tienen número físico, ordenar alfanuméricamente
            if (!a.pinNumber.empty() && !b.pinNumber.empty()) {
                return compareAlphanumeric(a.pinNumber, b.pinNumber);
            }
            // Pines sin número al final, ordenados por nombre
            if (a.pinNumber.empty() && !b.pinNumber.empty()) return false;
            if (!a.pinNumber.empty() && b.pinNumber.empty()) return true;
            return a.name < b.name;
        });

        // 6. Debug output detallado
        size_t linkageCount = std::count_if(pins.begin(), pins.end(),
            [](const PinInfo& p){ return p.type == "linkage"; });
        size_t bsrCount = std::count_if(pins.begin(), pins.end(),
            [](const PinInfo& p){ return p.inputCell != -1 || p.outputCell != -1; });
        size_t inputCount = std::count_if(pins.begin(), pins.end(),
            [](const PinInfo& p){ return p.type == "input"; });
        size_t outputCount = std::count_if(pins.begin(), pins.end(),
            [](const PinInfo& p){ return p.type == "output"; });
        size_t inoutCount = std::count_if(pins.begin(), pins.end(),
            [](const PinInfo& p){ return p.type == "inout"; });

        std::cout << "[DeviceModel] Loaded " << this->pins.size() << " pins from BSDL data:\n";
        std::cout << "  - " << linkageCount << " LINKAGE pins (VCC, GND, NC, etc.)\n";
        std::cout << "  - " << inputCount << " INPUT pins\n";
        std::cout << "  - " << outputCount << " OUTPUT pins\n";
        std::cout << "  - " << inoutCount << " INOUT pins\n";
        std::cout << "  - " << bsrCount << " pins with BSR cells\n";

        // DEBUG: Mostrar primeros 10 pines para verificar clasificación
        std::cout << "[DeviceModel] Sample of first 10 pins:\n";
        for (size_t i = 0; i < std::min(size_t(10), this->pins.size()); i++) {
            std::cout << "  Pin[" << i << "]: name=" << this->pins[i].name
                      << " pinNum=" << this->pins[i].pinNumber
                      << " type=" << this->pins[i].type
                      << " in=" << this->pins[i].inputCell
                      << " out=" << this->pins[i].outputCell << "\n";
        }
    }


    std::optional<PinInfo> DeviceModel::getPinInfo(const std::string& pinName) const {
        auto it = std::find_if(pins.begin(), pins.end(),
            [&pinName](const PinInfo& pin) { return pin.name == pinName; });
        return (it != pins.end()) ? std::make_optional(*it) : std::nullopt;
    }

    std::vector<std::string> DeviceModel::getPinNames() const {
        std::vector<std::string> names;
        for (const auto& pin : pins) names.push_back(pin.name);
        return names;
    }

    uint32_t DeviceModel::getInstruction(const std::string& instructionName) const {
        auto it = instructions.find(instructionName);
        return (it != instructions.end()) ? it->second : 0xFFFFFFFF;
    }

    std::string DeviceModel::getPinPort(const std::string& pinName) const {
        auto pinInfo = getPinInfo(pinName);
        return pinInfo ? pinInfo->port : "";
    }

    std::string DeviceModel::getPinType(const std::string& pinName) const {
        auto pinInfo = getPinInfo(pinName);
        return pinInfo ? pinInfo->type : "";
    }

    std::string DeviceModel::getPinNumber(const std::string& pinName) const {
        auto pinInfo = getPinInfo(pinName);
        return pinInfo ? pinInfo->pinNumber : "";
    }

} 