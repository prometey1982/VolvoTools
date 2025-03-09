#pragma once

#include "common/CarPlatform.hpp"
#include "common/VBF.hpp"

namespace flasher {

class SBLProviderBase {
public:
    virtual ~SBLProviderBase() {}
    virtual common::VBF getSBL(common::CarPlatform carPlatform, uint32_t ecuId, const std::string& additionalInfo) const = 0;
};

} // namespace flasher
