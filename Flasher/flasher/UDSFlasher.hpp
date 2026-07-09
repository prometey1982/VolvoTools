#pragma once

#include "FlasherCallback.hpp"
#include "FlasherBase.hpp"
#include "FlasherConfigs.hpp"

#include <common/CarPlatform.hpp>
#include <common/VBF.hpp>

#include <array>

class ICanChannel;

namespace j2534 {
    class J2534;
} // namespace j2534

namespace flasher {

    class UDSFlasher: public FlasherBase {
    public:
        UDSFlasher(j2534::J2534& j2534, common::CarPlatform carPlatform, uint32_t ecuId,
                   UDSFlasherConfig&& config);
        ~UDSFlasher();

    private:
        void startImpl(std::vector<std::unique_ptr<ICanChannel>>& channels) override;

        const UDSFlasherConfig _config;
    };

} // namespace flasher
