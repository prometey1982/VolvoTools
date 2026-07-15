#pragma once

#include "flasher/pin/PinCrackerSteps.hpp"

#include "common/CanIdProvider.hpp"
#include "common/ICanChannel.hpp"

namespace flasher {

class D2PinCrackerSteps final : public PinCrackerSteps {
public:
    explicit D2PinCrackerSteps(std::unique_ptr<common::CanIdProvider> canIdProvider,
                               uint8_t ecuId)
        : _canIdProvider{ std::move(canIdProvider) }
        , _ecuId{ ecuId }
    {}

    bool preAuth(common::ICanChannel& channel) override;

    void postAuth(common::ICanChannel& channel) override;

    bool tryPin(common::ICanChannel& channel, uint64_t pin) override;

    std::vector<unsigned long> startKeepAlive(common::ICanChannel& channel) override;

    void stopKeepAlive(std::vector<unsigned long>& ids) override;

    const common::CanIdProvider& getCanIdProvider() const override { return *_canIdProvider; }

private:
    std::unique_ptr<common::CanIdProvider> _canIdProvider;
    uint8_t _ecuId;
    std::vector<unsigned long> _keepAliveIds;
};

} // namespace flasher
