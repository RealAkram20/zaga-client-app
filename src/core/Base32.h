#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace zaga {

class Base32 {
public:
    static std::string encode(const std::vector<bool>& bits);

    static bool decode(const std::string& token, std::vector<bool>& bits);

private:
    static const char* alphabet();
};

}
