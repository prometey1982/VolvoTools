#pragma once

#include "flasher/FlasherBase.hpp"

#include <j2534/J2534.hpp>
#include <common/CarPlatform.hpp>
#include <common/J2534ChannelProvider.hpp>

namespace flasher {

class UDSMemoryReader: public FlasherBase {
public:
    UDSMemoryReader(j2534::J2534& j2534, common::CarPlatform carPlatform, uint8_t ecuId);


private:
    common::J2534ChannelProvider _j2534ChannelProvider;
    uint8_t _ecuId;
    std::vector<uint8_t> _buffer;
};

} // namespace flasher
