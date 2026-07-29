#pragma once

#include "ECUInfo.hpp"
#include "ProtocolType.hpp"

#include <string>
#include <vector>

namespace common {

struct BusConfiguration {
    std::string name;
    ProtocolType protocol;
    uint32_t baudrate;
    uint32_t canIdBitSize;
    std::vector<ECUInfo> ecuInfo;
};

} // namespace common
