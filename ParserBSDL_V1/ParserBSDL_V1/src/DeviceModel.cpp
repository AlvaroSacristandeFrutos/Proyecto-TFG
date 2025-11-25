#include "DeviceModel.h"
#include <iostream>
#include <algorithm>

DeviceModel::DeviceModel() {}

void DeviceModel::loadFromBSDL(const BSDLData& data) {
    std::cout << "--- Cargando Modelo de Dispositivo ---" << std::endl;

    // 1. CARGAR DATOS GLOBALES
    info.deviceName = data.entityName;
    info.idCode = data.idCode;
    info.pinTCK = data.tapTCK;
    info.pinTMS = data.tapTMS;
    info.pinTDI = data.tapTDI;
    info.pinTDO = data.tapTDO;
    info.pinTRST = data.tapTRST;
    info.boundaryLength = data.boundaryLength;
    info.instructionLength = data.instructionLength;
    info.packageName = data.physicalPinMap;
    info.instructionCapture = data.instructionCapture;

    for (const auto& instr : data.instructions) {
        if (!instr.opcodes.empty()) info.opcodes[instr.name] = instr.opcodes[0];
    }

    currentBitstream.resize(info.boundaryLength, false);
    info.bsrSafeState.resize(info.boundaryLength, false);

    // 2. GENERAR PINES (Fase A: Puertos)
    for (const auto& port : data.ports) {
        PinInfo pin;
        pin.logicalName = port.name;

        if (port.direction == "linkage") {
            std::string n = port.name;
            std::transform(n.begin(), n.end(), n.begin(), ::toupper);
            if (n.find("VCC") != std::string::npos || n.find("VDD") != std::string::npos) pin.type = PinType::POWER;
            else if (n.find("GND") != std::string::npos || n.find("VSS") != std::string::npos) pin.type = PinType::GROUND;
            else pin.type = PinType::UNKNOWN;
        }
        else {
            pin.type = PinType::IO;
        }

        if (data.pinMaps.count(port.name) && !data.pinMaps.at(port.name).empty()) {
            pin.physicalPin = data.pinMaps.at(port.name)[0];
        }
        pinMap[port.name] = pin;
    }

    // Fase B: Enriquecer con Boundary Scan (USANDO NUEVOS ENUMS)
    for (const auto& cell : data.boundaryCells) {
        auto it = pinMap.find(cell.portName);
        if (it == pinMap.end()) continue;

        PinInfo& pin = it->second;

        // Comparamos Enums (Rapidísimo)
        if (cell.function == CellFunction::INPUT || cell.function == CellFunction::CLOCK) {
            pin.inputCell = cell.cellNumber;
        }
        else if (cell.function == CellFunction::OUTPUT2 ||
            cell.function == CellFunction::OUTPUT3 ||
            cell.function == CellFunction::BIDIR) {

            pin.outputCell = cell.cellNumber;
            pin.controlCell = cell.controlCell;

            // Guardamos el safe value como string por compatibilidad con la struct actual, 
            // o lo convertimos al vuelo. 
            if (cell.safeValue == SafeBit::HIGH) pin.safeValue = "1";
            else if (cell.safeValue == SafeBit::LOW) pin.safeValue = "0";
            else pin.safeValue = "X";

            // Lógica Active Low: Si para deshabilitar se usa "1", es Active Low.
            if (cell.disableValue == SafeBit::HIGH) pin.activeLow = true;
            else pin.activeLow = false;

            if (cell.safeValue == SafeBit::HIGH) info.bsrSafeState[cell.cellNumber] = true;
        }
    }

    // 3. GENERAR ÍNDICE INVERSO
    for (auto& pair : pinMap) {
        if (!pair.second.physicalPin.empty()) {
            physicalMap[pair.second.physicalPin] = &pair.second;
        }
    }

    printSummary();
}

// ... (Resto de métodos iguales) ...
// Copia aquí los getters, setters y printSummary del DeviceModel.cpp anterior
// No cambian porque no usan la lógica interna de conversión.
const PinInfo* DeviceModel::getPin(const std::string& logicalName) const {
    auto it = pinMap.find(logicalName);
    if (it != pinMap.end()) return &it->second;
    return nullptr;
}

const PinInfo* DeviceModel::getPinByPhysical(const std::string& physicalName) const {
    auto it = physicalMap.find(physicalName);
    if (it != physicalMap.end()) return it->second;
    return nullptr;
}

void DeviceModel::setPinState(const std::string& logicalName, bool level) {
    auto it = pinMap.find(logicalName);
    if (it == pinMap.end()) return;

    const PinInfo& p = it->second;

    if (p.outputCell != -1 && p.outputCell < (int)currentBitstream.size()) {
        currentBitstream[p.outputCell] = level;
    }

    if (p.controlCell != -1 && p.controlCell < (int)currentBitstream.size()) {
        currentBitstream[p.controlCell] = !p.activeLow;
    }
}

void DeviceModel::printSummary() const {
    std::cout << "\n=== RESUMEN DEL MODELO ===" << std::endl;
    std::cout << "Dispositivo: " << info.deviceName << std::endl;
    std::cout << "Encapsulado: " << info.packageName << std::endl;
    std::cout << "Longitud BSR: " << info.boundaryLength << " bits" << std::endl;
    std::cout << "Pines Logicos: " << pinMap.size() << std::endl;
    std::cout << "Pines Fisicos Mapeados: " << physicalMap.size() << std::endl;
}