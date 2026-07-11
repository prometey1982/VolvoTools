#include "common/CanAlarmClock.hpp"

#include "common/protocols/D2ProtocolCommonSteps.hpp"
#include "common/protocols/UDSProtocolCommonSteps.hpp"
#include "common/CarPlatform.hpp"
#include "common/J2534ChannelProvider.hpp"
#include "common/J2534ChannelAdapter.hpp"

#include <array>

namespace common {

CanAlarmClock::CanAlarmClock(j2534::J2534& j2534)
    : _j2534{ j2534 }
{
}

void CanAlarmClock::start()
{
    std::array platforms{ common::CarPlatform::P2_250, common::CarPlatform::P2, common::CarPlatform::P3 };
    for(const auto& platform: platforms) {
        J2534ChannelProvider provider(_j2534, platform);
        auto channels{ provider.getAllChannels() };
        D2ProtocolCommonSteps::wakeUp(channels);
        UDSProtocolCommonSteps::wakeUp(channels);
    }
}

} // namespace common
