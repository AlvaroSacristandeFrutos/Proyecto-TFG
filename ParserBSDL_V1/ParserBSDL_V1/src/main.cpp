#include <iostream>
#include <iomanip>
#include <limits>
#include <map>
#include <vector>
#include <algorithm>
#include "BSDLParser.h"
#include "DeviceModel.h"

// Helper para extraer el nombre base de un pin vectorial (DATA(5) -> DATA)
std::string getBaseName(const std::string& pinName) {
    size_t parenPos = pinName.find('(');
    if (parenPos != std::string::npos) {
        return pinName.substr(0, parenPos);
    }
    return pinName;
}

int main(int argc, char* argv[]) {
    // ----------------------------------------------------------------
    // 1. CONFIGURACIÓN (Solo argumentos)
    // ----------------------------------------------------------------
    std::string filename;

    if (argc > 1) {
        filename = argv[1];
    }
    else {
        std::cerr << "[ERROR] No se ha proporcionado ningun archivo BSDL." << std::endl;
        std::cerr << "Uso: " << argv[0] << " <ruta_relativa_o_absoluta_al_archivo.bsd>" << std::endl;
        std::cerr << "Configure los argumentos en Visual Studio o arrastre el archivo." << std::endl;

        // Pausa de error
        std::cout << "\nPresiona ENTER para salir..." << std::endl;
        std::cin.ignore(std::numeric_limits<std::streamsize>::max(), '\n');
        std::cin.get();
        return 1;
    }

    std::cout << "==========================================" << std::endl;
    std::cout << "   TEST DE INTEGRIDAD BSDL Y VECTORES" << std::endl;
    std::cout << "==========================================" << std::endl;
    std::cout << "Leyendo archivo: " << filename << "\n" << std::endl;

    // ----------------------------------------------------------------
    // 2. PARSEO
    // ----------------------------------------------------------------
    BSDLParser parser;
    if (!parser.parse(filename)) {
        std::cerr << "[ERROR] Fallo al parsear el archivo." << std::endl;
        std::cerr << "Compruebe que la ruta es correcta y el archivo es accesible." << std::endl;
        std::cout << "Presiona ENTER para salir..." << std::endl;
        std::cin.get();
        return 1;
    }

    // ----------------------------------------------------------------
    // 3. CARGA DEL MODELO
    // ----------------------------------------------------------------
    DeviceModel chip;
    chip.loadFromBSDL(parser.getData());
    const ChipInfo& info = chip.getInfo();

    // ----------------------------------------------------------------
    // 4. VERIFICACIÓN DE DATOS GLOBALES
    // ----------------------------------------------------------------
    std::cout << "--- 1. IDENTIFICACION DEL CHIP ---" << std::endl;
    std::cout << std::left << std::setw(20) << "Entidad:" << info.deviceName << std::endl;
    std::cout << std::left << std::setw(20) << "Encapsulado:" << info.packageName << std::endl;

    std::cout << std::left << std::setw(20) << "IDCODE:";
    if (info.idCode != 0) {
        std::cout << "0x" << std::hex << std::uppercase << info.idCode << std::dec << " (OK)" << std::endl;
    }
    else {
        std::cout << "NO DETECTADO / 0" << std::endl;
    }

    std::cout << "\n--- 2. INTERFAZ JTAG (TAP) ---" << std::endl;
    std::cout << "TCK: " << (info.pinTCK.empty() ? "[FALTA]" : info.pinTCK) << " | ";
    std::cout << "TMS: " << (info.pinTMS.empty() ? "[FALTA]" : info.pinTMS) << " | ";
    std::cout << "TDI: " << (info.pinTDI.empty() ? "[FALTA]" : info.pinTDI) << " | ";
    std::cout << "TDO: " << (info.pinTDO.empty() ? "[FALTA]" : info.pinTDO) << std::endl;

    // ----------------------------------------------------------------
    // 5. VERIFICACIÓN DE EXPANSIÓN DE VECTORES
    // ----------------------------------------------------------------
    std::cout << "\n--- 3. ANALISIS DE PINES Y VECTORES ---" << std::endl;

    std::map<std::string, int> busCounts;
    int totalPins = 0;

    // Usamos los datos crudos del parser para verificar la generación
    const auto& parserData = parser.getData();
    for (const auto& port : parserData.ports) {
        std::string base = getBaseName(port.name);
        busCounts[base]++;
        totalPins++;
    }

    std::cout << "Total Pines Logicos detectados: " << totalPins << std::endl;
    std::cout << "Buses detectados (Vectores expandidos):" << std::endl;
    std::cout << "---------------------------------------" << std::endl;
    std::cout << std::left << std::setw(20) << "NOMBRE BUS" << "ANCHO (Pines)" << std::endl;
    std::cout << "---------------------------------------" << std::endl;

    bool vectorsFound = false;
    for (const auto& pair : busCounts) {
        if (pair.second > 1) {
            std::cout << std::left << std::setw(20) << pair.first << pair.second << " bits" << std::endl;
            vectorsFound = true;
        }
    }

    if (!vectorsFound) {
        std::cout << "[INFO] No se detectaron vectores (o el archivo usa pines escalares)." << std::endl;
    }
    else {
        std::cout << "[EXITO] Los vectores VHDL han sido procesados." << std::endl;
    }

    // ----------------------------------------------------------------
    // 6. PRUEBA DE MAPEO (PIN DETALLE)
    // ----------------------------------------------------------------
    std::cout << "\n--- 4. EJEMPLO DE DETALLE DE UN PIN ---" << std::endl;
    std::string testPin;
    if (!parserData.ports.empty()) {
        // Cogemos un pin del medio para probar
        testPin = parserData.ports[parserData.ports.size() / 2].name;
    }

    if (!testPin.empty()) {
        const PinInfo* pInfo = chip.getPin(testPin);
        if (pInfo) {
            std::cout << "Detalles del pin '" << testPin << "':" << std::endl;
            std::cout << "  - Fisico (Bola): " << (pInfo->physicalPin.empty() ? "Sin Mapeo" : pInfo->physicalPin) << std::endl;
            std::cout << "  - Tipo JTAG:     " << (pInfo->outputCell != -1 ? "SALIDA/BIDIR" : "ENTRADA/LINKAGE") << std::endl;
            if (pInfo->outputCell != -1) {
                std::cout << "  - Celda Output:  " << pInfo->outputCell << std::endl;
                std::cout << "  - Celda Control: " << pInfo->controlCell << " (Activo " << (pInfo->activeLow ? "BAJO" : "ALTO") << ")" << std::endl;
            }
        }
    }

    // ----------------------------------------------------------------
    // 7. CIERRE
    // ----------------------------------------------------------------
    std::cout << "\n==========================================" << std::endl;
    std::cout << "TEST FINALIZADO. Presiona ENTER para salir." << std::endl;

    // Limpieza de buffer segura
    // Nota: Si cin no tiene flags de error, ignore funciona bien.
    std::cin.clear();
    // Si hubo entrada previa, la ignoramos. Si no, esto no bloquea si se usa bien.
    // Usamos get() directamente que espera un enter.
    std::cin.get();

    return 0;
}