#ifndef BSDL_PARSER_H
#define BSDL_PARSER_H

#include <string>
#include <string_view>
#include <vector>
#include <map>
#include <cstdint> // Para uint8_t

struct Port {
    std::string name;
    std::string direction;
    std::string type;
};

struct Instruction {
    std::string name;
    std::vector<std::string> opcodes;
};

// --- NUEVOS ENUMS PARA AHORRAR MEMORIA ---
enum class CellFunction : uint8_t {
    INPUT, CLOCK, OUTPUT2, OUTPUT3, BIDIR, CONTROL, INTERNAL, UNKNOWN
};

enum class SafeBit : uint8_t {
    LOW, HIGH, DONT_CARE
};

struct BoundaryCell {
    int cellNumber = -1;
    std::string cellType; // "BC_1", "BC_4"... (Esto se suele dejar como string)
    std::string portName;

    // Optimización: Usamos enums (1 byte) en lugar de strings (32 bytes)
    CellFunction function = CellFunction::UNKNOWN;
    SafeBit safeValue = SafeBit::DONT_CARE;

    int controlCell = -1;
    SafeBit disableValue = SafeBit::DONT_CARE;
};

struct BSDLData {
    std::string entityName;
    std::string physicalPinMap;
    std::vector<Port> ports;
    std::map<std::string, std::vector<std::string>> pinMaps;

    uint32_t idCode = 0;

    std::string tapTCK;
    std::string tapTMS;
    std::string tapTDI;
    std::string tapTDO;
    std::string tapTRST;

    int instructionLength = 0;
    std::vector<Instruction> instructions;
    std::string instructionCapture;

    int boundaryLength = 0;
    std::vector<BoundaryCell> boundaryCells;
};

class BSDLParser {
private:
    BSDLData data;
    std::string fileBuffer;

    void parseBoundaryRegisterRaw(std::string_view content);
    void parseInstructionOpcodeRaw(std::string_view content);
    void parsePortsRaw(std::string_view content);
    void parsePinMapRaw(std::string_view content);

public:
    BSDLParser() = default;
    ~BSDLParser() = default;

    bool parse(const std::string& filename);
    const BSDLData& getData() const { return data; }
};

#endif // BSDL_PARSER_H