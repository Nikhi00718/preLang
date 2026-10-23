#pragma once

#include <string>

// A JSON string literal for `text`: quotes and backslashes are escaped, and control
// characters (which no message or path of the compiler should hold) become spaces.
inline std::string jsonString(const std::string& text) {
    std::string quoted = "\"";
    for (char c : text) {
        if (c == '\\' || c == '"') {
            quoted += '\\';
            quoted += c;
        } else if (static_cast<unsigned char>(c) < 0x20) {
            quoted += ' ';
        } else {
            quoted += c;
        }
    }
    return quoted + "\"";
}
