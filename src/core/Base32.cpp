#include "Base32.h"

#include <cctype>

namespace zaga {

const char* Base32::alphabet() {
    return "0123456789ABCDEFGHJKMNPQRSTVWXYZ";
}

std::string Base32::encode(const std::vector<bool>& bits) {
    std::string symbols;

    for (size_t i = 0; i + 5 <= bits.size(); i += 5) {
        int value = 0;
        for (int b = 0; b < 5; ++b) {
            value = (value << 1) | (bits[i + b] ? 1 : 0);
        }
        symbols += alphabet()[value];
    }

    std::string grouped;
    for (size_t i = 0; i < symbols.size(); ++i) {
        if (i > 0 && i % 5 == 0) {
            grouped += '-';
        }
        grouped += symbols[i];
    }

    return grouped;
}

bool Base32::decode(const std::string& token, std::vector<bool>& bits) {
    std::string normalized;

    for (char raw : token) {
        if (raw == ' ' || raw == '-' || raw == '\t') {
            continue;
        }

        char c = static_cast<char>(std::toupper(static_cast<unsigned char>(raw)));

        switch (c) {
            case 'O': c = '0'; break;
            case 'I': c = '1'; break;
            case 'L': c = '1'; break;
            case 'U': c = 'V'; break;
            default: break;
        }

        normalized += c;
    }

    bits.clear();
    bits.reserve(normalized.size() * 5);

    for (char c : normalized) {
        const char* found = nullptr;
        for (const char* p = alphabet(); *p; ++p) {
            if (*p == c) {
                found = p;
                break;
            }
        }

        if (found == nullptr) {
            return false;
        }

        int index = static_cast<int>(found - alphabet());
        for (int b = 4; b >= 0; --b) {
            bits.push_back(((index >> b) & 1) != 0);
        }
    }

    return true;
}

}
