#include "BSDLParser.h"
#include <iostream>
#include <iomanip>  
#include <algorithm> 
#include <vector>

// Función auxiliar para el test
std::string buscarPinFisico(const BSDLData& data, const std::string& nombrePin) {
    auto it = data.pinMaps.find(nombrePin);
    if (it != data.pinMaps.end() && !it->second.empty()) {
        return it->second[0];
    }
    return "";
}

void imprimirMapeoOrdenado(const BSDLData& data) {
    std::cout << "\n--- TEST DE MAPEO ---" << std::endl;

    std::vector<std::pair<std::string, std::vector<std::string>>> pinesOrdenados;

    for (const auto& par : data.pinMaps) {
        pinesOrdenados.push_back(par);
    }

    // Ordenar
    std::sort(pinesOrdenados.begin(), pinesOrdenados.end(),
        [](const std::pair<std::string, std::vector<std::string>>& a,
            const std::pair<std::string, std::vector<std::string>>& b) {
                return a.first < b.first;
        });

    // Imprimir
    for (const auto& par : pinesOrdenados) {
        std::cout << std::left << std::setw(15) << par.first << " -> ";
        for (const auto& p : par.second) std::cout << p << " ";
        std::cout << std::endl;
    }
}

int main(int argc, char* argv[]) {
    if (argc < 2) {
        std::cerr << "Uso: " << argv[0] << " <archivo.bsd>" << std::endl;
        return 1;
    }

    std::string archivo = argv[1];
    BSDLParser parser;

    std::cout << "Iniciando Test con archivo: " << archivo << std::endl;

    if (!parser.parse(archivo)) {
        std::cerr << "Error al parsear." << std::endl;
        return 1;
    }

    BSDLData data = parser.getData();
    imprimirMapeoOrdenado(data);

    return 0;
}