#include "common/protocols/UDSRequest.hpp"

#include "common/protocols/UDSError.hpp"
#include "common/CanFrame.hpp"
#include "common/ICanChannel.hpp"
#include "common/Util.hpp"

#define LOG_MODULE_NAME "common"
#include "common/LogHelper.hpp"

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
    : _canId{ canId }
    , _requestId{ getRequestId(data) }
    , _data{ data }
{
}

UDSRequest::UDSRequest(uint32_t canId, std::vector<uint8_t>&& data)
    : _canId{ canId }
    , _requestId{ getRequestId(data) }
    , _data{ std::move(data) }
{
}

std::vector<uint8_t> UDSRequest::process(ICanChannel& channel, size_t timeout)
{
    CanFrame request{ _canId, _data };
    if (!channel.send(request)) {
        throw std::runtime_error("Failed to send CAN message");
    }
    std::vector<uint8_t> result;
    while (true) {
        CanFrame response;
        if (!channel.receive(response, static_cast<unsigned long>(timeout))) {
            throw std::runtime_error("Failed to receive response");
        }
        try {
            checkUDSError(_requestId, response.data.data(), response.data.size());
        }
        catch (const UDSError& ex) {
            // 0x78 — не ошибка, а «ответ будет позже»: ждём следующий кадр.
            if (ex.getErrorCode() == UDSError::ErrorCode::RequestReceivedResponsePending) {
                LOG_MODULE(DEBUG) << "response pending (0x78), keep waiting";
                continue;
            }
            LOG_MODULE(ERROR) << "negative response: " << dumpArray(response.data)
                              << "(" << ex.what() << ")";
            throw;
        }
        if (response.data.size() < 1) {
            LOG_MODULE(DEBUG) << "empty response received, keep waiting";
            continue;
        }
        if (response.data[0] != _requestId + 0x40) {
            LOG_MODULE(DEBUG) << "response skipped, expected service 0x" << std::hex
                              << static_cast<int>(_requestId + 0x40) << ": "
                              << dumpArray(response.data);
            continue;
        }
        result = std::move(response.data);
        break;
    }
    return result;
}

std::vector<uint8_t> UDSRequest::process(ICanChannel& channel,
                                         const std::vector<uint8_t>& checkData,
                                         size_t retryCount, size_t timeout)
{
    channel.clearRx();
    CanFrame request{ _canId, _data };
    if (!channel.send(request)) {
        throw std::runtime_error("Failed to send CAN message");
    }
    std::vector<uint8_t> result;
    size_t remainingRetries = retryCount;
    while (remainingRetries > 0) {
        CanFrame response;
        if (!channel.receive(response, static_cast<unsigned long>(timeout))) {
            if (--remainingRetries == 0) {
                throw std::runtime_error("Failed to receive correct answer");
            }
            continue;
        }
        try {
            checkUDSError(_requestId, response.data.data(), response.data.size());
        }
        catch (const UDSError& ex) {
            if (ex.getErrorCode() == UDSError::ErrorCode::RequestReceivedResponsePending) {
                LOG_MODULE(DEBUG) << "response pending (0x78), keep waiting";
                continue;
            }
            LOG_MODULE(ERROR) << "negative response: " << dumpArray(response.data)
                              << "(" << ex.what() << ")";
            throw;
        }
        if (response.data.size() < 1 + checkData.size()) {
            LOG_MODULE(DEBUG) << "short response skipped: " << dumpArray(response.data);
            continue;
        }
        if (response.data[0] != _requestId + 0x40) {
            LOG_MODULE(DEBUG) << "response skipped, expected service 0x" << std::hex
                              << static_cast<int>(_requestId + 0x40) << ": "
                              << dumpArray(response.data);
            continue;
        }
        const bool match = std::equal(checkData.cbegin(), checkData.cend(),
                                       response.data.cbegin() + 1);
        if (!match) {
            LOG_MODULE(DEBUG) << "response check failed: " << dumpArray(response.data);
            if (--remainingRetries == 0) {
                throw std::runtime_error("Failed to receive correct answer");
            }
            continue;
        }
        result.assign(response.data.cbegin() + 1 + checkData.size(),
                      response.data.cend());
        return result;
    }
    throw std::runtime_error("Failed to receive correct answer");
}

} // namespace common
