#include "BSDLParser.h"
#include <iostream>
#include <fstream>
#include <algorithm>
#include <cctype>
#include <charconv>
#include <vector>

// --- HELPERS ESTÁTICOS ---

static std::string_view trim(std::string_view sv) {
    while (!sv.empty() && std::isspace(static_cast<unsigned char>(sv.front()))) sv.remove_prefix(1);
    while (!sv.empty() && std::isspace(static_cast<unsigned char>(sv.back()))) sv.remove_suffix(1);
    return sv;
}

static bool parseVhdlRange(std::string_view typeStr, int& start, int& end) {
    auto openParen = typeStr.find('(');
    auto closeParen = typeStr.find(')');
    if (openParen == std::string_view::npos || closeParen == std::string_view::npos) return false;
    auto rangeContent = trim(typeStr.substr(openParen + 1, closeParen - openParen - 1));
    std::string s(rangeContent); // Copia necesaria para transformación
    // En el parser parse() ya convertimos todo a mayúsculas, así que esto podría simplificarse,
    // pero lo dejamos por seguridad.
    size_t dirPos = s.find(" DOWNTO ");
    bool isDownto = (dirPos != std::string::npos);
    size_t splitPos = isDownto ? dirPos : s.find(" TO ");
    if (splitPos == std::string::npos) return false;
    try {
        start = std::stoi(s.substr(0, splitPos));
        end = std::stoi(s.substr(splitPos + (isDownto ? 8 : 4)));
        return true;
    }
    catch (...) { return false; }
}

static std::vector<std::string_view> splitSv(std::string_view str, char delimiter) {
    std::vector<std::string_view> tokens;
    size_t start = 0;
    size_t end = str.find(delimiter);
    while (end != std::string_view::npos) {
        tokens.push_back(str.substr(start, end - start));
        start = end + 1;
        end = str.find(delimiter, start);
    }
    tokens.push_back(str.substr(start));
    return tokens;
}

// --- NUEVOS HELPERS PARA ENUMS ---
static CellFunction stringToFunction(std::string_view s) {
    if (s == "INPUT") return CellFunction::INPUT;
    if (s == "CLOCK") return CellFunction::CLOCK;
    if (s == "OUTPUT2") return CellFunction::OUTPUT2;
    if (s == "OUTPUT3") return CellFunction::OUTPUT3;
    if (s == "BIDIR") return CellFunction::BIDIR;
    if (s == "CONTROL") return CellFunction::CONTROL;
    if (s == "INTERNAL") return CellFunction::INTERNAL;
    return CellFunction::UNKNOWN;
}

static SafeBit stringToSafeBit(std::string_view s) {
    if (s == "0") return SafeBit::LOW;
    if (s == "1") return SafeBit::HIGH;
    return SafeBit::DONT_CARE; // 'X' o cualquier otra cosa
}

// --- PARSER PRINCIPAL ---

bool BSDLParser::parse(const std::filesystem::path& filename) {
    // std::ifstream en C++17 acepta std::filesystem::path directamente
    // y maneja Unicode correctamente en todas las plataformas
    std::ifstream in(filename, std::ios::in | std::ios::binary);
    if (!in) return false;

    in.seekg(0, std::ios::end);
    std::string rawBuffer;
    rawBuffer.resize(in.tellg());
    in.seekg(0, std::ios::beg);
    in.read(&rawBuffer[0], rawBuffer.size());
    in.close();

    std::string cleanBuffer;
    cleanBuffer.reserve(rawBuffer.size());

    bool inComment = false;
    for (size_t i = 0; i < rawBuffer.size(); ++i) {
        char c = rawBuffer[i];
        if (!inComment && c == '-' && i + 1 < rawBuffer.size() && rawBuffer[i + 1] == '-') {
            inComment = true; i++; continue;
        }
        if (c == '\n' || c == '\r') {
            inComment = false; cleanBuffer += ' '; continue;
        }
        if (!inComment) {
            if (c == '\t') cleanBuffer += ' ';
            else cleanBuffer += std::toupper(static_cast<unsigned char>(c));
        }
    }

    fileBuffer = std::move(cleanBuffer);
    std::string_view content(fileBuffer);

    // 1. ENTITY
    if (auto pos = content.find("ENTITY"); pos != std::string_view::npos) {
        size_t nameStart = pos + 6;
        while (nameStart < content.size() && std::isspace(content[nameStart])) nameStart++;
        auto end = content.find(" IS", nameStart);
        if (end != std::string_view::npos) {
            data.entityName = std::string(trim(content.substr(nameStart, end - nameStart)));
        }
    }

    // 2. GENERIC
    if (auto pos = content.find("GENERIC"); pos != std::string_view::npos) {
        auto assignPos = content.find(":=", pos);
        if (assignPos != std::string_view::npos) {
            auto startQuote = content.find('"', assignPos);
            auto endSemi = content.find(";", assignPos);
            if (startQuote != std::string_view::npos && endSemi != std::string_view::npos && startQuote < endSemi) {
                auto endQuote = content.find('"', startQuote + 1);
                if (endQuote != std::string_view::npos && endQuote < endSemi) {
                    auto raw = content.substr(startQuote + 1, endQuote - startQuote - 1);
                    data.physicalPinMap = std::string(trim(raw));
                }
            }
        }
    }

    // 3. PORT
    if (auto pos = content.find("PORT"); pos != std::string_view::npos) {
        auto startParen = content.find('(', pos);
        if (startParen != std::string_view::npos) {
            auto endParen = content.find(");", startParen);
            if (endParen != std::string_view::npos) {
                parsePortsRaw(content.substr(startParen + 1, endParen - startParen - 1));
            }
        }
    }

    // 4. BOUNDARY LENGTH
    if (auto pos = content.find("BOUNDARY_LENGTH"); pos != std::string_view::npos) {
        auto start = content.find("IS", pos);
        auto end = content.find(";", pos);
        if (start != std::string_view::npos) {
            auto numSv = trim(content.substr(start + 2, end - (start + 2)));
            std::from_chars(numSv.data(), numSv.data() + numSv.size(), data.boundaryLength);
        }
    }

    if (auto pos = content.find("INSTRUCTION_LENGTH"); pos != std::string_view::npos) {
        auto start = content.find("IS", pos);
        auto end = content.find(";", pos);
        if (start != std::string_view::npos && end != std::string_view::npos) {
            auto numSv = trim(content.substr(start + 2, end - (start + 2)));
            // Convertir string a int
            std::from_chars(numSv.data(), numSv.data() + numSv.size(), data.instructionLength);
        }
    }

    // 5. INSTRUCTION OPCODE
    if (auto pos = content.find("INSTRUCTION_OPCODE"); pos != std::string_view::npos) {
        auto startQuote = content.find('"', pos);
        auto endSemi = content.find(";", pos);
        if (startQuote != std::string_view::npos && endSemi != std::string_view::npos) {
            auto endQuote = content.rfind('"', endSemi);
            if (endQuote > startQuote) {
                parseInstructionOpcodeRaw(content.substr(startQuote + 1, endQuote - startQuote - 1));
            }
        }
    }

    // 6. PIN MAP STRING
    if (auto pos = content.find("PIN_MAP_STRING"); pos != std::string_view::npos) {
        auto startQuote = content.find(":=", pos);
        auto endSemi = content.find(";", pos);
        if (startQuote != std::string_view::npos && endSemi != std::string_view::npos) {
            auto raw = trim(content.substr(startQuote + 2, endSemi - (startQuote + 2)));
            parsePinMapRaw(raw);
        }
    }

    // 7. BOUNDARY REGISTER (Aquí se usan los nuevos enums)
    auto brPos = content.find("BOUNDARY_REGISTER");
    if (brPos != std::string_view::npos) {
        auto startQuote = content.find('"', brPos);
        auto endQuote = content.rfind('"');
        if (startQuote != std::string_view::npos && endQuote != std::string_view::npos && endQuote > startQuote) {
            parseBoundaryRegisterRaw(content.substr(startQuote + 1, endQuote - startQuote - 1));
        }
    }

    // 8. IDCODE REGISTER
    if (auto pos = content.find("IDCODE_REGISTER"); pos != std::string_view::npos) {
        auto isPos = content.find(" IS ", pos);
        if (isPos != std::string_view::npos) {
            auto startQuote = content.find('"', isPos);
            auto endSemi = content.find(";", isPos);
            if (startQuote != std::string_view::npos && endSemi != std::string_view::npos) {
                auto rawId = trim(content.substr(startQuote + 1, endSemi - startQuote - 1));
                std::string binStr;
                for (char c : rawId) if (c == '0' || c == '1') binStr += c;
                if (!binStr.empty()) {
                    try { data.idCode = std::stoul(binStr, nullptr, 2); }
                    catch (...) { data.idCode = 0; }
                }
            }
        }
    }

    // 9. TAP SIGNALS
    auto parseTapAttr = [&](std::string_view attrName) -> std::string {
        auto pos = content.find(attrName);
        if (pos != std::string_view::npos) {
            auto ofPos = content.find(" OF ", pos);
            auto colonPos = content.find(" :", pos);
            if (ofPos != std::string_view::npos && colonPos != std::string_view::npos) {
                size_t start = ofPos + 4;
                return std::string(trim(content.substr(start, colonPos - start)));
            }
        }
        return "";
        };

    data.tapTCK = parseTapAttr("TAP_SCAN_CLOCK");
    data.tapTMS = parseTapAttr("TAP_SCAN_MODE");
    data.tapTDI = parseTapAttr("TAP_SCAN_IN");
    data.tapTDO = parseTapAttr("TAP_SCAN_OUT");
    data.tapTRST = parseTapAttr("TAP_SCAN_RESET");

    return true;
}

// --- RAW MEMBERS ---

void BSDLParser::parsePortsRaw(std::string_view content) {
    auto groups = splitSv(content, ';');
    for (auto group : groups) {
        group = trim(group);
        if (group.empty()) continue;
        auto colonPos = group.find(':');
        if (colonPos == std::string_view::npos) continue;
        auto namesPart = group.substr(0, colonPos);
        auto typePart = trim(group.substr(colonPos + 1));

        std::string dir = "in";
        auto has = [&](std::string_view haystack, std::string_view needle) {
            auto it = std::search(haystack.begin(), haystack.end(), needle.begin(), needle.end(),
                [](char a, char b) { return std::toupper(a) == std::toupper(b); });
            return it != haystack.end();
            };

        if (has(typePart, "INOUT")) dir = "inout";
        else if (has(typePart, "OUT")) dir = "out";
        else if (has(typePart, "BUFFER")) dir = "buffer";
        else if (has(typePart, "LINKAGE")) dir = "linkage";

        int vecStart = 0, vecEnd = 0;
        bool isVector = has(typePart, "VECTOR") && parseVhdlRange(typePart, vecStart, vecEnd);

        auto names = splitSv(namesPart, ',');
        for (auto name : names) {
            name = trim(name);
            if (name.empty()) continue;
            if (isVector) {
                int step = (vecStart <= vecEnd) ? 1 : -1;
                int current = vecStart;
                while (true) {
                    Port p;
                    p.name = std::string(name) + "(" + std::to_string(current) + ")";
                    p.direction = dir;
                    p.type = "bit";
                    data.ports.push_back(p);
                    if (current == vecEnd) break;
                    current += step;
                }
            }
            else {
                Port p;
                p.name = std::string(name);
                p.direction = dir;
                p.type = "bit";
                data.ports.push_back(p);
            }
        }
    }
}

void BSDLParser::parseInstructionOpcodeRaw(std::string_view content) {
    const char* ptr = content.data();
    const char* end = content.data() + content.size();
    while (ptr < end) {
        while (ptr < end && !std::isalnum(static_cast<unsigned char>(*ptr)) && *ptr != '(') ptr++;
        if (ptr >= end) break;
        if (*ptr == '(') { ptr++; continue; }
        const char* nameStart = ptr;
        while (ptr < end && (std::isalnum(static_cast<unsigned char>(*ptr)) || *ptr == '_')) ptr++;
        std::string name(nameStart, ptr - nameStart);
        while (ptr < end && *ptr != '(') ptr++;
        ptr++;
        const char* codeStart = ptr;
        while (ptr < end && (*ptr == '0' || *ptr == '1' || *ptr == 'X')) ptr++;
        std::string code(codeStart, ptr - codeStart);
        if (!name.empty() && !code.empty()) {
            Instruction instr; instr.name = name; instr.opcodes.push_back(code);
            data.instructions.push_back(instr);
        }
        while (ptr < end && *ptr != ',') ptr++;
        ptr++;
    }
}

void BSDLParser::parsePinMapRaw(std::string_view content) {
    std::string clean; clean.reserve(content.size());
    for (char c : content) if (c != '"' && c != '&') clean += c;
    std::string_view sv(clean);
    auto pairs = splitSv(sv, ',');
    for (auto pair : pairs) {
        auto colon = pair.find(':');
        if (colon != std::string_view::npos) {
            auto logic = trim(pair.substr(0, colon));
            auto phys = trim(pair.substr(colon + 1));
            if (!logic.empty() && !phys.empty()) {
                data.pinMaps[std::string(logic)].push_back(std::string(phys));
            }
        }
    }
}

void BSDLParser::parseBoundaryRegisterRaw(std::string_view content) {
    data.boundaryCells.reserve(data.boundaryLength > 0 ? data.boundaryLength : 100);
    const char* ptr = content.data();
    const char* end = content.data() + content.size();

    while (ptr < end) {
        while (ptr < end && !std::isdigit(static_cast<unsigned char>(*ptr))) ptr++;
        if (ptr == end) break;

        BoundaryCell cell;
        std::from_chars(ptr, end, cell.cellNumber);

        while (ptr < end && *ptr != '(') ptr++;
        if (ptr == end) break; ptr++;

        int fieldIdx = 0;
        while (ptr < end && *ptr != ')') {
            while (ptr < end && std::isspace(static_cast<unsigned char>(*ptr))) ptr++;
            const char* tokenStart = ptr;
            while (ptr < end && *ptr != ',' && *ptr != ')') ptr++;
            std::string_view token(tokenStart, ptr - tokenStart);
            token = trim(token);

            if (fieldIdx == 0) cell.cellType = std::string(token);
            else if (fieldIdx == 1) cell.portName = std::string(token);
            else if (fieldIdx == 2) cell.function = stringToFunction(token); // CONVERSIÓN A ENUM
            else if (fieldIdx == 3) cell.safeValue = stringToSafeBit(token); // CONVERSIÓN A ENUM
            else if (fieldIdx == 4) {
                if (token == "*") cell.controlCell = -1;
                else cell.controlCell = std::atoi(std::string(token).c_str());
            }
            else if (fieldIdx == 5) cell.disableValue = stringToSafeBit(token); // CONVERSIÓN A ENUM

            if (*ptr == ',') ptr++;
            fieldIdx++;
        }
        data.boundaryCells.push_back(std::move(cell));
    }
}