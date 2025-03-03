#pragma once

#include "VBF.hpp"
#include <istream>

namespace common {

class VBFParser {
public:
    VBFParser() = default;
    VBF parse(std::istream& data) const;
    VBF parse(const std::vector<char>& data) const;
    VBF parse(const std::vector<uint8_t>& data) const;
};

} // namespace common
