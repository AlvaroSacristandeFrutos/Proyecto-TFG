#pragma once

#include "../IJTAGAdapter.h" // <--- CRÍTICO: Aquí se definen AdapterType y AdapterDescriptor
#include <memory>
#include <string>
#include <vector>

namespace JTAG {

    /**
     * @brief Factory para crear instancias de adaptadores JTAG
     */
    class AdapterFactory {
    public:
        // Factory method principal
        static std::unique_ptr<IJTAGAdapter> create(AdapterType type);

        // Helpers de conversión
        static std::unique_ptr<IJTAGAdapter> createFromString(const std::string& typeName);
        static std::string typeToString(AdapterType type);
        static AdapterType stringToType(const std::string& typeName);

        // Detección de hardware
        static bool isSupported(AdapterType type);
        static std::vector<AdapterType> getSupportedAdapters();

        // Lista estática de adaptadores disponibles (no verifica conexión física)
        static std::vector<AdapterDescriptor> getAvailableAdapters();

    private:
        // Clase estática pura
        AdapterFactory() = delete;
        ~AdapterFactory() = delete;
    };

} // namespace JTAG