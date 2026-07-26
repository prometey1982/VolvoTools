#pragma once

#include "flasher/pin/PinCracker.hpp"
#include "flasher/pin/PinCrackerStorage.hpp"


#include <j2534/J2534.hpp>

#include <functional>
#include <memory>

namespace flasher {

std::unique_ptr<PinCracker> createDummyCracker(
    j2534::J2534& j2534,
    common::CarPlatform carPlatform,
    uint32_t ecuId,
    PinCracker::Direction direction,
    uint64_t startPin,
    std::function<void(PinCracker::State, uint64_t)> stateCallback,
    PinCrackerStorage& storage);

std::unique_ptr<PinCracker> createPinCracker(
    j2534::J2534& j2534,
    common::CarPlatform carPlatform,
    uint32_t ecuId,
    PinCracker::Direction direction,
    uint64_t startPin,
    std::function<void(PinCracker::State, uint64_t)> stateCallback,
    PinCrackerStorage& storage);

} // namespace flasher
