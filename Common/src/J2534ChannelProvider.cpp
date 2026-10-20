#include "common/J2534ChannelProvider.hpp"
#include "common/J2534ChannelAdapter.hpp"
#include "common/CanIdProvider.hpp"
#include "common/ProtocolType.hpp"

#include "common/CommonData.hpp"
#include "common/Util.hpp"

#include <j2534/J2534.hpp>
#include <j2534/J2534Channel.hpp>

#include <chrono>
#include <stdexcept>
#include <thread>

namespace common {

namespace {

std::unique_ptr<j2534::J2534Channel> createRawChannelByBusConf(j2534::J2534& j2534, BusConfiguration bus, ECUInfo ecuInfo)
{
    if(ecuInfo.protocol == ProtocolType::CAN) {
        const unsigned long flags = (bus.canIdBitSize == 29)? CAN_29BIT_ID : 0;
        if(bus.baudrate != 125000) {
            return openChannel(j2534, static_cast<unsigned long>(ecuInfo.protocol), flags, bus.baudrate);
        }
        else {
            return openLowSpeedChannel(j2534, flags);
        }
    }
    else if(ecuInfo.protocol == ProtocolType::ISO15765) {
        // Для 29-битных шин CANIdentifier в data.yaml отсутствует (там только Address),
        // поэтому физический ID запроса строим через CanIdProvider: 0x18DA{ps}F1.
        // Для 11-битных шин результат совпадает с CANIdentifier из data.yaml.
        uint32_t canId{ ecuInfo.canId };
        if (bus.canIdBitSize == 11 || bus.canIdBitSize == 29) {
            canId = createCanIdProvider(
                ecuInfo.protocol, bus.canIdBitSize, ecuInfo.ecuId, ecuInfo.canId)->getPhysCanId();
        }
        return openUDSChannel(j2534, bus.baudrate, canId, bus.canIdBitSize);
    }
    else if(ecuInfo.protocol == ProtocolType::ISO14230) {
        return openTP20Channel(j2534, bus.baudrate, ecuInfo.canId);
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
    , _bridgeChannel{ openBridgeChannelIfNeeded(_j2534, _carPlatform) }
{
    // WORKAROUND: if bridge channel is opened then we need to wait some time
    // to open relay.
    if(_bridgeChannel) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
}

J2534ChannelProvider::~J2534ChannelProvider()
{
}

j2534::J2534& J2534ChannelProvider::getJ2534() const
{
    return _j2534;
}

std::vector<std::unique_ptr<ICanChannel>> J2534ChannelProvider::getAllChannels(uint32_t ecuId) const
{
    std::vector<std::unique_ptr<ICanChannel>> result;
    const auto conf{ getConfigurationInfoByCarPlatform(_carPlatform) };
    for(const auto& bus: conf.busInfo) {
        for(const auto& ecu: bus.ecuInfo) {
            if(ecu.ecuId == ecuId) {
                auto rawChannel{ createRawChannelByBusConf(_j2534, bus, ecu) };
                if (rawChannel) {
                    result.emplace_back(std::make_unique<J2534ChannelAdapter>(
                        std::move(rawChannel), bus.canIdBitSize == 29));
                }
            }
        }
    }
    return result;
}

std::unique_ptr<ICanChannel> J2534ChannelProvider::getChannelForEcu(uint32_t ecuId) const
{
    const auto ecuInfo{ getEcuInfoByEcuId(_carPlatform, ecuId) };
    const auto& bus{ std::get<0>(ecuInfo) };
    auto rawChannel{ createRawChannelByBusConf(_j2534, bus, std::get<1>(ecuInfo)) };
    if (rawChannel) {
        return std::make_unique<J2534ChannelAdapter>(std::move(rawChannel), bus.canIdBitSize == 29);
    }
    return {};
}

} // namespace common
