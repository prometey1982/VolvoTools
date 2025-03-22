#pragma once

#include "FlasherCallback.hpp"

#include "FlasherBase.hpp"

#include <common/protocols/RequestProcessorBase.hpp>
#include <common/CarPlatform.hpp>
#include <common/VBF.hpp>

#include <array>

namespace j2534 {
    class J2534;
} // namespace j2534

namespace flasher {

    struct KWPFlasherParameters {
        const std::array<uint8_t, 5> pin;
    };

    class KWPFlasher: public FlasherBase {
    public:
        KWPFlasher(j2534::J2534& j2534, const common::RequestProcessorBase& requestProcessor,
            FlasherParameters&& flasherParameters, KWPFlasherParameters&& kwpFlasherParameters);
        ~KWPFlasher();

    private:
        void startImpl(std::vector<std::unique_ptr<j2534::J2534Channel>>& channels);

    private:
        const common::RequestProcessorBase& _requestProcessor;
        KWPFlasherParameters _kwpFlasherParameters;
    };

} // namespace flasher
