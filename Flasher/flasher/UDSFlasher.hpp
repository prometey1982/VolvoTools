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

    struct UDSFlasherParameters {
        const std::array<uint8_t, 5> pin;
        const common::VBF flash;
    };

    class UDSFlasher: public FlasherBase {
    public:
        UDSFlasher(common::J2534Info& j2534Info, FlasherParameters&& flasherParaneters, UDSFlasherParameters&& udsFlasherParameters);
        ~UDSFlasher();

    private:
        void startImpl();

    private:
        const std::vector<common::ConfigurationInfo> _configurationInfo;
        UDSFlasherParameters _udsFlasherParameters;
    };

} // namespace flasher
