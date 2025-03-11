#pragma once

#include "CarPlatform.hpp"
#include "VBF.hpp"

namespace common {

VBF loadVBFForFlasher(CarPlatform carPlatform, uint8_t ecuId,
                      const std::string& additionalData, const std::string& path,
                      std::istream& input);

}
