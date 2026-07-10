#pragma once

#include <cstdint>
#include <vector>

namespace common {

    struct CanFrame {
    uint32_t id = 0;
    std::vector<uint8_t> data;
    bool isExtendedId = false;
};

} // namespace common
