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

    class UDSFlasher: public FlasherBase {
    public:
        UDSFlasher(j2534::J2534& j2534, common::CommonStepData&& commonStepData, const std::array<uint8_t, 5>& pin, const common::VBF& bootloader, const common::VBF& flash);
        ~UDSFlasher();

    private:
        void startImpl();

    private:
        std::unique_ptr<class UDSFlasherImpl> _impl;
    };

} // namespace flasher
