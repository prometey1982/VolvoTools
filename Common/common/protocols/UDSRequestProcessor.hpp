#pragma once

#include "RequestProcessorBase.hpp"


namespace common {
    class ICanChannel;

    class UDSRequestProcessor: public RequestProcessorBase {
    public:
        explicit UDSRequestProcessor(ICanChannel& channel, const uint32_t canId);

        virtual std::vector<uint8_t> process(std::vector<uint8_t>&& service, std::vector<uint8_t>&& params, size_t timeout) const override;
        virtual void disconnect() override;
        virtual bool connect() override;

    private:
        ICanChannel& _channel;
        const uint32_t _canId;
    };

} // namespace common
