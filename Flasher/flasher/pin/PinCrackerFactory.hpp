#pragma once

#include "flasher/pin/PinCracker.hpp"
#include "flasher/pin/PinCrackerStorage.hpp"

#include <common/CarPlatform.hpp>
#include <common/ICanChannel.hpp>

#include <functional>
#include <memory>
#include <vector>

namespace flasher {

std::unique_ptr<PinCracker> createDummyCracker(
    common::CarPlatform carPlatform,
    uint32_t ecuId,
    PinCracker::Direction direction,
    uint64_t startPin,
    std::function<void(PinCracker::State, uint64_t)> stateCallback,
    PinCrackerStorage& storage);

std::unique_ptr<PinCracker> createPinCracker(
    std::vector<std::unique_ptr<common::ICanChannel>> channels,
    common::CarPlatform carPlatform,
    uint32_t ecuId,
    PinCracker::Direction direction,
    uint64_t startPin,
    std::function<void(PinCracker::State, uint64_t)> stateCallback,
    PinCrackerStorage& storage);

} // namespace flasher
