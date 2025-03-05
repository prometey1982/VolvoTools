#pragma once

#include "FlasherCallback.hpp"

#include <common/CarPlatform.hpp>
#include <common/VBF.hpp>
#include <common/UDSCommonStepData.hpp>
#include <common/UDSProtocolBase.hpp>

#include <array>
#include <memory>
#include <mutex>

namespace j2534 {
    class J2534;
    class J2534Channel;
} // namespace j2534

namespace flasher {

    class UDSFlasher : public common::UDSProtocolBase {
    public:
        UDSFlasher(j2534::J2534& j2534, common::CommonStepData&& commonStepData, const std::array<uint8_t, 5>& pin, const common::VBF& bootloader, const common::VBF& flash);
        ~UDSFlasher();

        void start();

    private:
        std::array<uint8_t, 5> _pin;
        common::VBF _bootloader;
        common::VBF _flash;
        common::CommonStepData _commonStepData;
        std::vector<FlasherCallback*> _callbacks;
        std::thread _thread;
    };

} // namespace flasher
