#pragma once

#include <cstdint>
#include <vector>

struct CanFrame {
    uint32_t id = 0;
    std::vector<uint8_t> data;
    bool isExtendedId = false;
};
