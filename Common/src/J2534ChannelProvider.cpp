#include "common/J2534ChannelProvider.hpp"

#include "common/CommonData.hpp"
#include "common/Util.hpp"

#include <j2534/J2534.hpp>
#include <j2534/J2534Channel.hpp>

#include <easylogging++.h>

#include <stdexcept>

namespace common {

namespace {

std::unique_ptr<j2534::J2534Channel> createChannelByBusConf(j2534::J2534& j2534, BusConfiguration bus, uint32_t canId = 0)
{
    if(bus.protocolId == CAN) {
        const unsigned long flags = (bus.canIdBitSize == 29)? CAN_29BIT_ID : 0;
        if(bus.baudrate != 125000) {
            return openChannel(j2534, bus.protocolId, flags, bus.baudrate);
        }
        else {
            return openLowSpeedChannel(j2534, flags);
        }
    }
    else if(bus.protocolId == ISO15765) {
        return openUDSChannel(j2534, bus.baudrate, canId);
    }
    else if(bus.protocolId == ISO14230) {
        return openTP20Channel(j2534, bus.baudrate, canId);
    }
    throw std::runtime_error("Unsupported protocol");
}

std::unique_ptr<j2534::J2534Channel> openBridgeChannelIfNeeded(j2534::J2534& j2534, CarPlatform carPlatform)
{
    const auto conf{ getConfigurationInfoByCarPlatform(carPlatform) };
    for(const auto& bus: conf.busInfo) {
        if(bus.baudrate == 250000) {
            return openBridgeChannel(j2534);
        }
    }
    return {};
}

}

J2534ChannelProvider::J2534ChannelProvider(j2534::J2534& j2534, CarPlatform carPlatform)
    : _j2534{ j2534 }
    , _carPlatform{ carPlatform }
    , _bridgeChannel{}
{
    LOG(INFO) << "J2534ChannelProvider init platform=" << static_cast<int>(_carPlatform);
    _bridgeChannel = openBridgeChannelIfNeeded(_j2534, _carPlatform);
    LOG(INFO) << "J2534ChannelProvider init done, bridge="
        << (_bridgeChannel ? "open" : "not-needed");
}

J2534ChannelProvider::~J2534ChannelProvider()
{
}

j2534::J2534& J2534ChannelProvider::getJ2534() const
{
    return _j2534;
}

std::vector<std::unique_ptr<j2534::J2534Channel>> J2534ChannelProvider::getAllChannels(uint32_t ecuId) const
{
    std::vector<std::unique_ptr<j2534::J2534Channel>> result;
    const auto conf{ getConfigurationInfoByCarPlatform(_carPlatform) };
    LOG(INFO) << "J2534ChannelProvider getAllChannels enter ecu=0x" << std::hex
        << ecuId << " buses=" << std::dec << conf.busInfo.size();
    for(const auto& bus: conf.busInfo) {
        uint32_t canId{};
        for(const auto& ecu: bus.ecuInfo) {
            if(ecu.ecuId == ecuId) {
                canId = ecu.canId;
            }
        }
        LOG(INFO) << "J2534ChannelProvider opening bus protocol=" << bus.protocolId
            << " baudrate=" << bus.baudrate << " canId=0x" << std::hex << canId;
        result.emplace_back(createChannelByBusConf(_j2534, bus, canId));
        LOG(INFO) << "J2534ChannelProvider bus opened, total=" << std::dec << result.size();
    }
    LOG(INFO) << "J2534ChannelProvider getAllChannels exit count=" << result.size();
    return result;
}

std::vector<std::unique_ptr<j2534::J2534Channel>> J2534ChannelProvider::getUdsChannels(uint32_t ecuId) const
{
    std::vector<std::unique_ptr<j2534::J2534Channel>> result;
    const auto conf{ getConfigurationInfoByCarPlatform(_carPlatform) };
    LOG(INFO) << "J2534ChannelProvider getUdsChannels enter ecu=0x" << std::hex
        << ecuId << " buses=" << std::dec << conf.busInfo.size();
    for(const auto& bus: conf.busInfo) {
        if (bus.protocolId != ISO15765) {
            continue;
        }
        uint32_t canId{};
        for(const auto& ecu: bus.ecuInfo) {
            if(ecu.ecuId == ecuId) {
                canId = ecu.canId;
            }
        }
        LOG(INFO) << "J2534ChannelProvider opening UDS bus baudrate=" << bus.baudrate
            << " canId=0x" << std::hex << canId;
        result.emplace_back(createChannelByBusConf(_j2534, bus, canId));
        LOG(INFO) << "J2534ChannelProvider UDS bus opened, total=" << std::dec << result.size();
    }
    LOG(INFO) << "J2534ChannelProvider getUdsChannels exit count=" << result.size();
    return result;
}

std::unique_ptr<j2534::J2534Channel> J2534ChannelProvider::getChannelForEcu(uint32_t ecuId) const
{
    const auto ecuInfo{ getEcuInfoByEcuId(_carPlatform, ecuId) };
    LOG(INFO) << "J2534ChannelProvider getChannelForEcu ecu=0x" << std::hex << ecuId
        << " protocol=" << std::dec << std::get<0>(ecuInfo).protocolId
        << " baudrate=" << std::get<0>(ecuInfo).baudrate
        << " canId=0x" << std::hex << std::get<1>(ecuInfo).canId;
    return createChannelByBusConf(_j2534, std::get<0>(ecuInfo), std::get<1>(ecuInfo).canId);
}

} // namespace common
