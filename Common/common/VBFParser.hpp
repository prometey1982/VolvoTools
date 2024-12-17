#pragma once

#include "VBF.hpp"

#include <istream>

namespace common {

class VBFParser {
public:
    VBFParser();
    VBF parse(std::istream& data);
};

} // namespace common
