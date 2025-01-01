#pragma once

#include "ECUInfo.hpp"

#include <string>
#include <vector>

namespace common {

struct BusConfiguration {
    std::string name;
    uint32_t protocolId;
    uint32_t baudrate;
    uint32_t canIdBitSize;
    std::vector<ECUInfo> ecuInfo;
};

} // namespace common
