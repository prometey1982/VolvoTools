#pragma once

#include <string>

namespace common {

struct ECUInfo {
    uint32_t ecuId;
    uint32_t canId;
    std::string name;
};

} // namespace common
