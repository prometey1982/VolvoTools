#include "common/J2534Info.hpp"

#include "common/CommonData.hpp"
#include "common/Util.hpp"

#include <j2534/J2534.hpp>
#include <j2534/J2534Channel.hpp>

#include <stdexcept>

namespace common {

J2534Info::J2534Info(std::unique_ptr<j2534::J2534> j2534)
    : _configurationInfo{ common::loadConfiguration(CommonData::commonConfiguration) }
    , _j2534{ std::move(j2534) }
    , _carPlatform{ CarPlatform::Undefined }
{
}

J2534Info::~J2534Info()
{
}

void J2534Info::openChannels(CarPlatform carPlatform)
{
    const auto conf{ getConfigurationInfoByCarPlatform(_configurationInfo, carPlatform) };
    for(const auto& bus: conf.busInfo) {
        if(bus.protocolId == CAN) {
            const unsigned long flags = (bus.canIdBitSize == 29)? CAN_29BIT_ID : 0;
            if(bus.baudrate != 125) {
                _channels.emplace_back(openChannel(getJ2534(), bus.protocolId, flags, bus.baudrate));
            }
            else {
                _channels.emplace_back(openLowSpeedChannel(getJ2534(), flags));
            }
        }
        else if(bus.protocolId == ISO15765) {
            _channels.emplace_back(openUDSChannel(getJ2534(), bus.baudrate));
        }
    }
    _carPlatform = carPlatform;
}

void J2534Info::closeChannels()
{
    _carPlatform = CarPlatform::Undefined;
    _channels.clear();
}

j2534::J2534& J2534Info::getJ2534() const
{
    if(!_j2534) {
        throw std::runtime_error("J2534 is empty");
    }
    return *_j2534;
}

const std::vector<std::unique_ptr<j2534::J2534Channel>>& J2534Info::getChannels() const
{
    return _channels;
}

j2534::J2534Channel& J2534Info::getChannelForEcu(uint32_t ecuId) const
{
    return getChannelByEcuId(_configurationInfo, _carPlatform, ecuId, _channels);
}

j2534::J2534Channel& J2534Info::getChannelForCM(CMType cmType) const
{
    switch(cmType) {
    case CMType::ECM_ME7:
    case CMType::ECM_ME9_P1:
        return getChannelForEcu(0x7A);
    case CMType::ECM_DENSO_P3:
    case CMType::ECM_ME9_P3:
        return getChannelForEcu(0x10);
    case CMType::TCM_AW55_P2:
    case CMType::TCM_TF80_P2:
        return getChannelForEcu(0x6E);
    case CMType::TCM_TF80_P3:
        return getChannelForEcu(0x18);
    }
    throw std::runtime_error("Unsupported control module");
}

} // namespace common
