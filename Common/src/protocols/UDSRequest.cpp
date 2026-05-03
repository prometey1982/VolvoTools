#include "common/protocols/UDSRequest.hpp"

#include "common/protocols/UDSError.hpp"
#include "common/Util.hpp"

#include <easylogging++.h>

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

std::vector<uint8_t> payloadFromFrame(const uint8_t* data, size_t dataSize)
{
    if (dataSize <= 4) {
        return {};
    }
    return { data + 4, data + dataSize };
}

std::vector<uint8_t> frameToVector(const uint8_t* data, size_t dataSize)
{
    return { data, data + dataSize };
}

}

UDSRequest::UDSRequest(uint32_t canId, const std::vector<uint8_t>& data)
    : _canId{ canId }
    , _requestId{ getRequestId(data) }
    , _data{ data }
    , _message{ canId, data }
{
}

UDSRequest::UDSRequest(uint32_t canId, std::vector<uint8_t>&& data)
    : _canId{ canId }
    , _requestId{ getRequestId(data) }
    , _data{ std::move(data) }
    , _message{ canId, _data }
{
}

std::vector<uint8_t> UDSRequest::process(const j2534::J2534Channel& channel, size_t timeout)
{
    unsigned long numMsgs = 0;
    LOG(DEBUG) << "UDS TX can=0x" << std::hex << _canId
               << " data=" << toHexString(_data) << " timeout=" << std::dec << timeout;
    const auto writeStatus = channel.writeMsgs(_message, numMsgs, timeout);
    if(writeStatus != STATUS_NOERROR || numMsgs < 1) {
        LOG(ERROR) << "UDS TX failed can=0x" << std::hex << _canId
                   << " status=" << j2534StatusToString(writeStatus) << " written=" << std::dec << numMsgs;
        throw std::runtime_error("Failed to send CAN message: " + j2534StatusToString(writeStatus));
    }
    std::vector<uint8_t> result;
    channel.readMsgs([&result, this](const uint8_t* data, size_t dataSize) {
        LOG(DEBUG) << "UDS RX raw=" << toHexString(frameToVector(data, dataSize));
        try {
            checkUDSError(_requestId, data, dataSize);
        }
        catch (const UDSError& ex) {
            LOG(WARNING) << "UDS NRC request=0x" << std::hex << static_cast<int>(_requestId)
                         << " nrc=0x" << static_cast<int>(ex.getErrorCode()) << " " << ex.what();
            if (ex.getErrorCode() == UDSError::ErrorCode::RequestReceivedResponsePending) {
                return true;
            }
            throw;
        }
        if(dataSize < 5) {
            return true;
        }
        if(data[4] != _requestId + 0x40) {
            return true;
        }
        result.reserve(result.size() + dataSize);
        std::copy(data, data + dataSize, std::back_inserter(result));
        LOG(DEBUG) << "UDS RX accepted payload=" << toHexString(payloadFromFrame(data, dataSize));
        return false;
    }, timeout);
    if(result.empty()) {
        LOG(WARNING) << "UDS RX completed without payload/no matching response can=0x" << std::hex << _canId
                     << " request=0x" << static_cast<int>(_requestId);
    }
    return result;
}

std::vector<uint8_t> UDSRequest::process(const j2534::J2534Channel& channel,
                                         const std::vector<uint8_t>& checkData,
                                         size_t retryCount, size_t timeout)
{
    channel.clearRx();
    unsigned long numMsgs = 0;
    LOG(DEBUG) << "UDS TX can=0x" << std::hex << _canId
               << " data=" << toHexString(_data) << " expect=" << toHexString(checkData)
               << " timeout=" << std::dec << timeout;
    const auto writeStatus = channel.writeMsgs(_message, numMsgs, timeout);
    if(writeStatus != STATUS_NOERROR || numMsgs < 1) {
        LOG(ERROR) << "UDS TX failed can=0x" << std::hex << _canId
                   << " status=" << j2534StatusToString(writeStatus) << " written=" << std::dec << numMsgs;
        throw std::runtime_error("Failed to send CAN message: " + j2534StatusToString(writeStatus));
    }
    std::vector<uint8_t> result;
    channel.readMsgs([&result, &checkData, &retryCount, this](const uint8_t* data, size_t dataSize) {
        LOG(DEBUG) << "UDS RX raw=" << toHexString(frameToVector(data, dataSize));
        try {
            checkUDSError(_requestId, data, dataSize);
        }
        catch (const UDSError& ex) {
            LOG(WARNING) << "UDS NRC request=0x" << std::hex << static_cast<int>(_requestId)
                         << " nrc=0x" << static_cast<int>(ex.getErrorCode()) << " " << ex.what();
            if (ex.getErrorCode() == UDSError::ErrorCode::RequestReceivedResponsePending) {
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
            return true;
        }
        dataOffset += checkData.size();
        result.reserve(result.size() + dataSize);
        std::copy(data + dataOffset, data + dataSize, std::back_inserter(result));
        LOG(DEBUG) << "UDS RX accepted payload=" << toHexString(payloadFromFrame(data, dataSize));
        return false;
    }, timeout);
    if(result.empty()) {
        LOG(WARNING) << "UDS RX completed without payload/no matching response can=0x" << std::hex << _canId
                     << " request=0x" << static_cast<int>(_requestId);
    }
    return result;
}

} // namespace common
