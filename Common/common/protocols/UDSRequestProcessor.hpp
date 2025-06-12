#pragma once

#include "RequestProcessorBase.hpp"

#include "j2534/J2534Channel.hpp"

namespace common {

    class UDSRequestProcessor: public RequestProcessorBase {
    public:
        explicit UDSRequestProcessor(const j2534::J2534Channel& channel, const uint32_t canId);

        virtual std::vector<uint8_t> process(std::vector<uint8_t>&& service, std::vector<uint8_t>&& params, size_t timeout) const override;
        virtual void disconnect() override;
        virtual bool connect() override;

    private:
        const j2534::J2534Channel& _channel;
        const uint32_t _canId;
    };

} // namespace common
