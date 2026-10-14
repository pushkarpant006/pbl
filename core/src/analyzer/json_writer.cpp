
#include "json_writer.hpp"

#include <cstdio>
#include <iostream>

namespace safebox {
namespace analyzer {

std::string escapeJson(const std::string& value) {
    const char* hex = "0123456789abcdef";
    std::string escaped;
    for (int index = 0; index < static_cast<int>(value.size()); index++) {
        unsigned char character = static_cast<unsigned char>(value[index]);
        if (character == '"' || character == '\\') escaped.push_back('\\');
        if (character == '\n') escaped += "\\n";
        else if (character == '\r') escaped += "\\r";
        else if (character == '\t') escaped += "\\t";
        else if (character < 0x20) {
            escaped += "\\u00";
            escaped.push_back(hex[character >> 4]);
            escaped.push_back(hex[character & 0x0f]);
        } else escaped.push_back(static_cast<char>(character));
    }
    return escaped;
}

void writeJsonLine(const std::string& record) {
    std::cout << record << "\n";
    std::fflush(stdout);
}

}}