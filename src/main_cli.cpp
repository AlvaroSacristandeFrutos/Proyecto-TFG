/**
 * @file main_cli.cpp
 * @brief JTAG Boundary Scanner - Selector de Sonda y Ejecución
 */

#include "controller/ScanController.h"
#include <iostream>
#include <iomanip>
#include <memory>
#include <limits> // Para std::numeric_limits

 // Helper para esperar pulsación de tecla antes de cerrar
void waitForExit() {
    std::cout << "\nPresiona ENTER para salir...";
    std::cin.ignore(std::numeric_limits<std::streamsize>::max(), '\n');
    std::cin.get();
}

int main() {
    std::cout << "========================================\n";
    std::cout << "   JTAG BOUNDARY SCANNER - SYSTEM INIT  \n";
    std::cout << "========================================\n\n";

    try {
        // ---------------------------------------------------------
        // 1. INICIALIZAR CONTROLADOR
        // ---------------------------------------------------------
        auto controller = std::make_unique<JTAG::ScanController>();

        // ---------------------------------------------------------
        // 2. DETECTAR SONDAS DISPONIBLES
        // ---------------------------------------------------------
        std::cout << "[Scanning] Buscando adaptadores compatibles...\n";
        auto probes = controller->getDetectedAdapters();

        if (probes.empty()) {
            std::cerr << " [!] CRITICO: No se han encontrado adaptadores (ni siquiera Mock).\n";
            waitForExit();
            return 1;
        }

        // ---------------------------------------------------------
        // 3. MENÚ DE SELECCIÓN DE USUARIO
        // ---------------------------------------------------------
        std::cout << "Se han encontrado " << probes.size() << " sonda(s):\n\n";

        for (size_t i = 0; i < probes.size(); ++i) {
            std::cout << "  [" << (i + 1) << "] " << probes[i].name
                << " (" << probes[i].serialNumber << ")\n";
        }

        int selection = 0;
        while (selection < 1 || selection >(int)probes.size()) {
            std::cout << "\nElige una sonda (1-" << probes.size() << "): ";
            if (!(std::cin >> selection)) {
                std::cin.clear(); // Limpiar flag de error
                std::cin.ignore(10000, '\n'); // Descartar entrada incorrecta
            }
        }

        // Limpiar el buffer después de leer el número
        std::cin.ignore(std::numeric_limits<std::streamsize>::max(), '\n');

        JTAG::AdapterDescriptor selectedProbe = probes[selection - 1];
        std::cout << "\n----------------------------------------\n";
        std::cout << "Has seleccionado: " << selectedProbe.name << "\n";
        std::cout << "----------------------------------------\n";

        // ---------------------------------------------------------
        // 4. CONEXIÓN
        // ---------------------------------------------------------
        std::cout << "[Conectando...] ";
        if (!controller->connectAdapter(selectedProbe.type, 1000000)) { // 1 MHz
            std::cerr << "FALLO.\n [!] No se pudo conectar a la sonda.\n";
            waitForExit();
            return 1;
        }
        std::cout << "OK.\n";
        std::cout << "  Info: " << controller->getAdapterInfo() << "\n\n";

        // ---------------------------------------------------------
        // 5. DETECCIÓN DE CHIP (IDCODE)
        // ---------------------------------------------------------
        std::cout << "[JTAG Chain] Leyendo IDCODE...\n";
        uint32_t id = controller->detectDevice();
        std::cout << "  --> IDCODE: 0x" << std::hex << std::setfill('0')
            << std::setw(8) << id << std::dec << "\n";

        if (id == 0) {
            std::cerr << " [!] No se detecta ningún chip. Revisa el cableado.\n";
            // Nota: Con MockAdapter es normal recibir 0 en loopback si enviamos 1s.
            // Continuamos para la demo.
            if (selectedProbe.type != JTAG::AdapterType::MOCK) {
                waitForExit();
                return 1;
            }
            std::cout << "  (Continuando de todas formas por ser simulacion)\n";
        }
        std::cout << "\n";

        // ---------------------------------------------------------
        // 6. CARGAR BSDL (MODELO DEL CHIP)
        // ---------------------------------------------------------
        std::cout << "[BSDL Loader] Cargando fichero de definicion...\n";

        // Rutas de búsqueda (relativa a build o relativa a root)
        std::string bsdlPath = "../test_files/ejemplo.bsd";
        if (!controller->loadBSDL(bsdlPath)) {
            bsdlPath = "test_files/ejemplo.bsd";
            if (!controller->loadBSDL(bsdlPath)) {
                std::cerr << " [!] Error fatal: No se encuentra 'test_files/ejemplo.bsd'\n";
                waitForExit();
                return 1;
            }
        }
        std::cout << "  --> Dispositivo identificado: " << controller->getDeviceName() << "\n\n";

        // ---------------------------------------------------------
        // 7. INICIALIZACIÓN Y TEST
        // ---------------------------------------------------------
        std::cout << "[Engine] Inicializando Boundary Scan (Reset -> Sample -> Extest)...\n";
        if (!controller->initialize()) {
            std::cerr << " [!] Error al inicializar la cadena de escaneo.\n";
            return 1;
        }
        std::cout << "  --> Sistema listo para control manual.\n\n";

        // ---------------------------------------------------------
        // 8. INTERACCIÓN CON PINES (DEMO)
        // ---------------------------------------------------------
        std::cout << "=== PRUEBA DE CONTROL DE PINES (INTEL MAX 10) ===\n";

        // Pines reales del BSDL cargado
        struct PinTest { std::string name; JTAG::PinLevel level; };
        std::vector<PinTest> testSequence = {
            {"IOA3", JTAG::PinLevel::HIGH},
            {"IOD7", JTAG::PinLevel::LOW},
            {"IOB4", JTAG::PinLevel::HIGH}
        };

        // A) ESCRITURA
        std::cout << "1. Escribiendo valores en registro de salida...\n";
        for (const auto& test : testSequence) {
            std::cout << "   Set " << test.name << " -> "
                << (test.level == JTAG::PinLevel::HIGH ? "HIGH" : "LOW") << " ... ";

            if (controller->setPin(test.name, test.level)) {
                std::cout << "OK\n";
            }
            else {
                std::cout << "ERROR (Pin no existe)\n";
            }
        }

        // B) APLICACIÓN (JTAG SHIFT)
        std::cout << "2. Aplicando cambios (Shift-DR)...\n";
        controller->applyChanges();
        std::cout << "   --> Bits enviados al chip.\n";

        // C) LECTURA
        std::cout << "3. Leyendo estado actual (Sample)...\n";
        controller->samplePins(); // Captura estado real

        std::cout << "4. Verificando resultados:\n";
        for (const auto& test : testSequence) {
            auto val = controller->getPin(test.name);
            std::string stateStr = "UNKNOWN";
            if (val) stateStr = (*val == JTAG::PinLevel::HIGH) ? "HIGH" : "LOW";

            std::cout << "   Pin " << test.name << ": Leido = " << stateStr << "\n";
        }

        std::cout << "\n========================================\n";
        std::cout << "   DEMO FINALIZADA CON EXITO\n";
        std::cout << "========================================\n";

        controller->disconnectAdapter();
        waitForExit();
        return 0;

    }
    catch (const std::exception& e) {
        std::cerr << "\n[!] EXCEPCION NO CONTROLADA: " << e.what() << "\n";
        waitForExit();
        return 1;
    }
}