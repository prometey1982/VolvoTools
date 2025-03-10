#include "common/D2Request.hpp"

#include "common/Util.hpp"

#include <algorithm>
#include <stdexcept>
#include <iterator>

namespace common {

D2Request::D2Request(uint8_t ecuId, const std::vector<uint8_t>& data)
    : _message{ ecuId, data }
{
}

D2Request::D2Request(uint8_t ecuId, std::vector<uint8_t>&& data)
    : _message{ ecuId, std::move(data) }
{
}

D2Request::D2Request(D2Message&& message)
    : _message{ std::move(message) }
{
}

std::vector<uint8_t> D2Request::process(const j2534::J2534Channel& channel, size_t timeout)
{
    unsigned long numMsgs = 0;
    if(channel.writeMsgs(_message, numMsgs, timeout) != STATUS_NOERROR || numMsgs < 1) {
        throw std::runtime_error("Failed to send CAN message");
    }
    const uint8_t ecuId = _message.getEcuId();
    const auto requestId = _message.getRequestId();
    bool firstMessage = true;
    std::vector<uint8_t> result;
    channel.readMsgs([&result, ecuId, requestId, &firstMessage, this](const uint8_t* data, size_t dataSize) {
        checkD2Error(ecuId, requestId, data, dataSize);
        if(dataSize < 7) {
            return true;
        }
        if(data[4] != 0x8F || data[5] != ecuId || data[6] != requestId[0]) {
            return true;
        }
        result.reserve(result.size() + dataSize);
        std::copy(data, data + dataSize, std::back_inserter(result));
        firstMessage = false;
        return false;
    }, timeout);
    return result;
}

} // namespace common
