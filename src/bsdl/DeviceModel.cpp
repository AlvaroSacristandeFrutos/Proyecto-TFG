#include "DeviceModel.h"
#include <algorithm>
#include <iostream>

namespace JTAG {

    DeviceModel::DeviceModel() {
        // Constructor vacío. El modelo nace inválido hasta que se llama a loadFromData()
    }

    void DeviceModel::loadFromData(const BSDLData& data) {
        this->deviceName = data.entityName;
        this->idcode = data.idCode;
        this->bsrLength = data.boundaryLength;
        this->irLength = data.instructionLength;

        // 1. Cargar Instrucciones
        this->instructions.clear();
        for (const auto& instr : data.instructions) {
            // El parser devuelve opcodes como strings binarios ("00101")
            // Convertimos el primero de la lista a uint32
            if (!instr.opcodes.empty()) {
                try {
                    uint32_t code = std::stoul(instr.opcodes[0], nullptr, 2);
                    this->instructions[instr.name] = code;
                }
                catch (...) {}
            }
        }

        // 2. Mapear Pines (La parte difícil)
        // El BSDL define celdas (BoundaryCells). Nosotros queremos Pines (PinInfo).
        // Agrupamos las celdas por nombre de puerto.
        this->pins.clear();
        std::map<std::string, PinInfo> tempPinMap;

        for (const auto& cell : data.boundaryCells) {
            if (cell.portName == "*") continue; // Celdas de control internas

            // Crear entrada si no existe
            if (tempPinMap.find(cell.portName) == tempPinMap.end()) {
                tempPinMap[cell.portName] = { cell.portName, -1, -1, -1 };
            }

            // Asignar índice de celda según función
            if (cell.function == CellFunction::INPUT) {
                tempPinMap[cell.portName].inputCell = cell.cellNumber;
            }
            else if (cell.function == CellFunction::OUTPUT2 || cell.function == CellFunction::OUTPUT3) {
                tempPinMap[cell.portName].outputCell = cell.cellNumber;
                if (cell.controlCell != -1) {
                    tempPinMap[cell.portName].controlCell = cell.controlCell;
                }
            }
            else if (cell.function == CellFunction::BIDIR) {
                // BIDIR suele tener input y output en celdas distintas o compartidas según tecnología
                // Simplificación: asumimos que esta celda actúa como output
                tempPinMap[cell.portName].outputCell = cell.cellNumber;
            }
        }

        // Convertir mapa a vector final
        for (auto const& [name, info] : tempPinMap) {
            this->pins.push_back(info);
        }

        std::cout << "[DeviceModel] Loaded " << pins.size() << " pins from BSDL data.\n";
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

} 