#pragma once

#include "FlasherBase.hpp"
#include "FlasherConfigs.hpp"

#include <common/compression/CompressionType.hpp>
#include <common/encryption/EncryptionType.hpp>
#include <common/protocols/RequestProcessorBase.hpp>
#include <common/CarPlatform.hpp>
#include <common/VBF.hpp>

#include <array>

class ICanChannel;

namespace j2534 {
    class J2534;
} // namespace j2534

namespace flasher {

    class KWPFlasher: public FlasherBase {
    public:
        KWPFlasher(j2534::J2534& j2534,
            common::CarPlatform carPlatform, uint32_t ecuId,
            KWPFlasherConfig&& config);
        ~KWPFlasher();

    private:
        void startImpl(std::vector<std::unique_ptr<ICanChannel>>& channels) override;

        const KWPFlasherConfig _config;
    };

} // namespace flasher
