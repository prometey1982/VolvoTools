#pragma once

#include "FlasherCallback.hpp"

#include "FlasherBase.hpp"

#include <common/CarPlatform.hpp>
#include <common/VBF.hpp>
#include <common/UDSCommonStepData.hpp>

#include <array>

namespace j2534 {
    class J2534;
} // namespace j2534

namespace flasher {

    struct UDSFlasherParameters {
        const std::array<uint8_t, 5> pin;
    };

    class UDSFlasher: public FlasherBase {
    public:
        UDSFlasher(j2534::J2534& j2534, FlasherParameters&& flasherParameters, UDSFlasherParameters&& udsFlasherParameters);
        ~UDSFlasher();

    private:
        void startImpl(std::vector<std::unique_ptr<j2534::J2534Channel>>& channels);

    private:
        UDSFlasherParameters _udsFlasherParameters;
    };

} // namespace flasher
