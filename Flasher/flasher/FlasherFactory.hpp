#pragma once

#include <array>
#include <common/CarPlatform.hpp>
#include <cstdint>
#include <memory>

namespace flasher {

class IFlasherFactoryParametersProvider {
public:
    enum class FlashingType {
        Flashing,
        Reading,
        ReadingByChecksum
    };

    virtual ~IFlasherFactoryParametersProvider() {}

    virtual common::CarPlatform getCarPlatform() const = 0;
    virtual uint8_t getEcuId() const;
    std::array<uint8_t, 5> getPin() const;
    FlashingType getFlashingType() const;
};

class FlasherBase;

class FlasherFactory {
public:
    virtual ~FlasherFactory() {}
    virtual std::unique_ptr<FlasherBase> createFlasher(IFlasherFactoryParametersProvider& parametersProvider) const;
};

}
