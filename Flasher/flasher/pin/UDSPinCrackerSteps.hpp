#pragma once

#include "flasher/pin/PinCrackerSteps.hpp"

#include "common/CanIdProvider.hpp"
#include "common/ICanChannel.hpp"

namespace flasher {

class UDSPinCrackerSteps final : public PinCrackerSteps {
public:
    UDSPinCrackerSteps(std::unique_ptr<common::CanIdProvider> canIdProvider,
                       bool needProgSession = false)
        : _canIdProvider{ std::move(canIdProvider) }
        , _needProgSession{ needProgSession }
    {}

    bool preAuth(common::ICanChannel& channel) override;

    void postAuth(common::ICanChannel& channel) override;

    bool tryPin(common::ICanChannel& channel, uint64_t pin) override;

    std::vector<unsigned long> startKeepAlive(common::ICanChannel& channel) override;

    void stopKeepAlive(std::vector<unsigned long>& ids) override;

    std::chrono::milliseconds getRetryDelay() const override { return std::chrono::milliseconds{5000}; }

    const common::CanIdProvider& getCanIdProvider() const override { return *_canIdProvider; }

private:
    std::unique_ptr<common::CanIdProvider> _canIdProvider;
    bool _needProgSession;
    std::vector<unsigned long> _keepAliveIds;
};

} // namespace flasher
