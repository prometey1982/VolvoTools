#include "common/UDSRequest.hpp"

#include "common/Util.hpp"

#include <algorithm>
#include <stdexcept>
#include <iterator>

namespace common {

namespace {

uint8_t getRequestId(const std::vector<uint8_t>& data)
{
    if(data.empty()) {
        throw std::runtime_error("Can't get request id from empty request");
    }
    return data[0];
}

}

UDSRequest::UDSRequest(uint32_t canId, const std::vector<uint8_t>& data)
    : _requestId{ getRequestId(data) }
    , _message{ canId, data }
{
}

UDSRequest::UDSRequest(uint32_t canId, std::vector<uint8_t>&& data)
    : _requestId{ getRequestId(data) }
    , _message{ canId, std::move(data) }
{
}

std::vector<uint8_t> UDSRequest::process(const j2534::J2534Channel& channel, size_t timeout)
{
    unsigned long numMsgs = 0;
    if(channel.writeMsgs(_message, numMsgs, timeout) != STATUS_NOERROR || numMsgs < 1) {
        throw std::runtime_error("Failed to send CAN message");
    }
    std::vector<uint8_t> result;
    channel.readMsgs([&result, this](const uint8_t* data, size_t dataSize) {
        checkUDSError(_requestId, data, dataSize);
        if(dataSize < 5) {
            return true;
        }
        if(data[4] != _requestId + 0x40) {
            return true;
        }
        result.reserve(result.size() + dataSize);
        std::copy(data, data + dataSize, std::back_inserter(result));
        return false;
    }, timeout);
    return result;
}

} // namespace common
