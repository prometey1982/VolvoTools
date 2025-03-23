#include "common/protocols/UDSRequestProcessor.hpp"

#include "common/protocols/UDSRequest.hpp"

namespace common {

    UDSRequestProcessor::UDSRequestProcessor(const j2534::J2534Channel& channel, uint32_t canId)
        : _channel{ channel }
        , _canId{ canId }
    {
    }

    std::vector<uint8_t> UDSRequestProcessor::process(std::vector<uint8_t>&& service, std::vector<uint8_t>&& params, size_t timeout) const
    {
        UDSRequest request{ _canId, std::move(service) };
        return request.process(_channel, timeout);
    }

    void UDSRequestProcessor::disconnect()
    {
    }

    bool UDSRequestProcessor::connect()
    {
        return true;
    }

} // namespace common
