#pragma once

#include "common/ICanChannel.hpp"
#include "common/CanIdProvider.hpp"
#include "common/J2534ChannelProvider.hpp"

#include <chrono>
#include <memory>
#include <vector>

namespace flasher {

class PinCrackerSteps {
public:
    virtual ~PinCrackerSteps() = default;

    virtual bool preAuth(common::ICanChannel& channel) = 0;
    virtual void postAuth(common::ICanChannel& channel) = 0;

    virtual bool tryPin(common::ICanChannel& channel, uint64_t pin) = 0;

    virtual std::vector<unsigned long> startKeepAlive(common::ICanChannel& channel) { return {}; }
    virtual void stopKeepAlive(std::vector<unsigned long>& ids) { (void)ids; }

    virtual uint64_t getMinPin() const { return 0; }
    virtual uint64_t getMaxPin() const { return 0xFFFFFF; }

    virtual std::chrono::milliseconds getRetryDelay() const { return std::chrono::milliseconds{0}; }

    virtual const common::CanIdProvider& getCanIdProvider() const = 0;
};

} // namespace flasher
