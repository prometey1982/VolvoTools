#include "common/CanAlarmClock.hpp"
#include "common/CanIdProvider.hpp"

#include "common/protocols/D2ProtocolCommonSteps.hpp"
#include "common/protocols/UDSProtocolCommonSteps.hpp"
#include "common/CarPlatform.hpp"
#include "common/J2534ChannelProvider.hpp"
#include "common/J2534ChannelAdapter.hpp"
#include "common/utility.hpp"

#define LOG_MODULE_NAME "common"
#include "common/LogHelper.hpp"

#include <j2534/J2534_v0404.h>

#include <array>

namespace common {

CanAlarmClock::CanAlarmClock(j2534::J2534& j2534)
    : _j2534{ j2534 }
{
}

void CanAlarmClock::start()
{
    std::array platforms{ common::CarPlatform::P2_250, common::CarPlatform::P2, common::CarPlatform::P2_UDS, common::CarPlatform::P3 };
    for(const auto& platform: platforms) {
        J2534ChannelProvider provider(_j2534, platform);
        try {
            auto channels{ provider.getAllChannels() };

            D2ProtocolCommonSteps::wakeUp(channels);

            auto udsProvider = createCanIdProvider(ISO15765, 11, 0, 0, 0x33);
            auto funcCanId = udsProvider->getFuncCanId();
            UDSProtocolCommonSteps::wakeUp(channels, funcCanId);
            }
        catch(const std::exception &ex)
        {
            LOG_MODULE(ERROR) << "carPlatform(" << to_underlying(platform) << "), " << ex.what();
        }
        catch(...)
        {
            LOG_MODULE(ERROR) << "carPlatform(" << to_underlying(platform) << "), unknown exception";
        }
    }
}

} // namespace common
