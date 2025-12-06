#ifndef BSDLCATALOG_H
#define BSDLCATALOG_H

#include <string>
#include <map>
#include <optional>

namespace JTAG {

class BSDLCatalog {
public:
    BSDLCatalog() = default;

    // Escanea directorio y construye índice IDCODE → path
    bool scanDirectory(const std::string& directory);

    // Busca archivo BSDL por IDCODE
    std::optional<std::string> findByIDCODE(uint32_t idcode) const;

    // Número de dispositivos indexados
    size_t size() const { return idcodeMap.size(); }

private:
    // Extrae IDCODE parseando archivo BSDL
    std::optional<uint32_t> extractIDCODE(const std::string& bsdlPath);

    std::map<uint32_t, std::string> idcodeMap; // IDCODE → path
};

} // namespace JTAG

#endif // BSDLCATALOG_H
