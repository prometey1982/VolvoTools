#pragma once

#include "IChannel.hpp"

namespace common {

class IDevice {
public:
    virtual ~IDevice() {}
    virtual IChannel getChannel(ChannelType channelType) = 0;
};

}
