#include "common/protocols/UDSRequest.hpp"

#include "common/protocols/UDSError.hpp"
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

std::vector<uint8_t> UDSRequest::process(const j2534::J2534Channel& channel,
                                         const std::vector<uint8_t>& checkData,
                                         size_t retryCount, size_t timeout)
{
    channel.clearRx();
    unsigned long numMsgs = 0;
    if(channel.writeMsgs(_message, numMsgs, timeout) != STATUS_NOERROR || numMsgs < 1) {
        throw std::runtime_error("Failed to send CAN message");
    }
    std::vector<uint8_t> result;
    channel.readMsgs([&result, &checkData, &retryCount, this](const uint8_t* data, size_t dataSize) {
        try {
            checkUDSError(_requestId, data, dataSize);
        }
        catch (const UDSError& ex) {
            if (UDSError::ErrorCode::RequestReceivedResponsePending) {
                return true;
            }
            throw;
        }
        size_t dataOffset = 4;
        if(dataSize < dataOffset + checkData.size()) {
            return true;
        }
        if(data[dataOffset] != _requestId + 0x40) {
            return true;
        }
        ++dataOffset;
        const auto areResultEqual{std::equal(checkData.cbegin(), checkData.cend(), data + dataOffset)};
        if (!areResultEqual) {
            if(--retryCount == 0) {
                throw std::runtime_error("Failed to receive correct answer");
            }
            return (--retryCount != 0);
        }
        dataOffset += checkData.size();
        result.reserve(result.size() + dataSize);
        std::copy(data + dataOffset, data + dataSize, std::back_inserter(result));
        return false;
    }, timeout);
    return result;
}

} // namespace common
