#include "flasher/pin/PinCrackerFactory.hpp"

#include "flasher/pin/PinCrackerSteps.hpp"
#include "flasher/pin/UDSPinCrackerSteps.hpp"
#include "flasher/pin/D2PinCrackerSteps.hpp"
#include "flasher/pin/DummyPinCrackerSteps.hpp"

#include <common/CanIdProvider.hpp>
#include <common/CarPlatform.hpp>
#include <common/ICanChannel.hpp>
#include <common/J2534ChannelProvider.hpp>
#include <common/Util.hpp>

#include <vector>

namespace flasher {

std::unique_ptr<PinCrackerSteps> createPinCrackerStepsForBus(
    const common::BusConfiguration& bus,
    uint32_t ecuId,
    bool needProgSession)
{
    auto provider = common::createCanIdProvider(
        bus.protocolId, bus.canIdBitSize, ecuId, 0, 0x33);

    if (bus.protocolId == ISO15765) {
        return std::make_unique<UDSPinCrackerSteps>(
            std::move(provider), needProgSession);
    }

    if (bus.protocolId == CAN && bus.canIdBitSize == 29) {
        return std::make_unique<D2PinCrackerSteps>(
            std::move(provider), static_cast<uint8_t>(ecuId));
    }

    return nullptr;
}

std::unique_ptr<PinCrackerSteps> createDummyCrackerStepsForBus(
    const common::BusConfiguration& bus,
    uint32_t ecuId,
    [[maybe_unused]]bool needProgSession)
{
    auto provider = common::createCanIdProvider(
        bus.protocolId, bus.canIdBitSize, ecuId, 0, 0x33);

    return std::make_unique<DummyPinCrackerSteps>(
        std::move(provider), ecuId);
}

std::unique_ptr<PinCracker> createCrackerImpl(
    j2534::J2534& j2534,
    common::CarPlatform carPlatform,
    uint32_t ecuId,
    PinCracker::Direction direction,
    uint64_t startPin,
    std::function<void(PinCracker::State, uint64_t)> stateCallback,
    PinCrackerStorage& storage,
    std::function<std::unique_ptr<PinCrackerSteps>(const common::BusConfiguration&, uint32_t, bool)> stepsFactory)
{
    const auto conf = common::getConfigurationInfoByCarPlatform(carPlatform);
    const auto [ecuBusInfo, ecuInfo] = common::getEcuInfoByEcuId(carPlatform, ecuId);
    (void)ecuInfo;

    common::J2534ChannelProvider channelProvider(j2534, carPlatform);
    auto channels = channelProvider.getAllChannels(ecuId);

    if (channels.empty()) {
        throw std::runtime_error("No channels available for PIN cracking");
    }

    std::vector<PinCracker::BusContext> buses;
    size_t ecuBusIndex = 0;
    bool foundEcu = false;

    for (size_t i = 0; i < channels.size() && i < conf.busInfo.size(); ++i) {
        const auto& bus = conf.busInfo[i];

        auto steps = stepsFactory(bus, ecuId, false);
        if (!steps) {
            continue;
        }

        if (bus.baudrate == ecuBusInfo.baudrate && !foundEcu) {
            ecuBusIndex = buses.size();
            foundEcu = true;
        }

        buses.push_back({
            std::move(channels[i]),
            std::move(steps)
        });
    }

    if (!foundEcu) {
        throw std::runtime_error("Could not determine ECU bus channel");
    }

    return std::make_unique<PinCracker>(
        std::move(buses),
        ecuBusIndex,
        direction,
        startPin,
        std::move(stateCallback),
        storage);
}

std::unique_ptr<PinCracker> createDummyCracker(
    j2534::J2534& j2534,
    common::CarPlatform carPlatform,
    uint32_t ecuId,
    PinCracker::Direction direction,
    uint64_t startPin,
    std::function<void(PinCracker::State, uint64_t)> stateCallback,
    PinCrackerStorage& storage)
{
    const auto conf = common::getConfigurationInfoByCarPlatform(carPlatform);
    const auto [ecuBusInfo, ecuInfo] = common::getEcuInfoByEcuId(carPlatform, ecuId);
    (void)ecuInfo;

    std::vector<PinCracker::BusContext> buses;
    size_t ecuBusIndex = 0;
    bool foundEcu = false;

    for (size_t i = 0; i < conf.busInfo.size(); ++i) {
        const auto& bus = conf.busInfo[i];

        auto steps = createDummyCrackerStepsForBus(bus, ecuId, false);
        if (!steps) {
            continue;
        }

        if (bus.baudrate == ecuBusInfo.baudrate && !foundEcu) {
            ecuBusIndex = buses.size();
            foundEcu = true;
        }

        buses.push_back({
            {},
            std::move(steps)
        });
    }

    if (!foundEcu) {
        throw std::runtime_error("Could not determine ECU bus channel");
    }

    return std::make_unique<PinCracker>(
        std::move(buses),
        ecuBusIndex,
        direction,
        startPin,
        std::move(stateCallback),
        storage);
}

std::unique_ptr<PinCracker> createPinCracker(
    j2534::J2534& j2534,
    common::CarPlatform carPlatform,
    uint32_t ecuId,
    PinCracker::Direction direction,
    uint64_t startPin,
    std::function<void(PinCracker::State, uint64_t)> stateCallback,
    PinCrackerStorage& storage)
{
    return createCrackerImpl(j2534, carPlatform, ecuId, direction, startPin,
                             stateCallback, storage, createPinCrackerStepsForBus);
}

} // namespace flasher
