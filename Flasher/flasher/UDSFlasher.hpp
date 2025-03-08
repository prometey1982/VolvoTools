#pragma once

#include "FlasherCallback.hpp"

#include "FlasherBase.hpp"

#include <common/CarPlatform.hpp>
#include <common/VBF.hpp>
#include <common/UDSCommonStepData.hpp>
#include <common/UDSProtocolBase.hpp>

#include <array>
#include <memory>
#include <mutex>

namespace j2534 {
    class J2534;
} // namespace j2534

namespace flasher {

    struct UDSFlasherData {
        const common::CarPlatform carPlatform;
        const uint32_t ecuId;
        const std::array<uint8_t, 5> pin;
        const common::VBF bootloader;
        const common::VBF flash;
    };

    class UDSFlasher: public FlasherBase {
    public:
        UDSFlasher(j2534::J2534& j2534, const UDSFlasherData& flasherData);
        ~UDSFlasher();

    private:
        void startImpl();

    private:
        const UDSFlasherData _flasherData;
    };

} // namespace flasher
