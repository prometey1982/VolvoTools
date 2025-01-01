#pragma once

#include "BusConfiguration.hpp"

#include <vector>

namespace common {

struct ConfigurationInfo {
    std::string name;
    std::vector<BusConfiguration> busInfo;
};

} // namespace common
