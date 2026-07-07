#include "common/protocols/D2Request.hpp"

#include "common/CanFrame.hpp"
#include "common/ICanChannel.hpp"
#include "common/Util.hpp"

#include <algorithm>
#include <stdexcept>
#include <iterator>
#include <thread>

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

D2Request::D2Request(const D2Message& message)
    : _message{ message }
{
}

std::vector<uint8_t> D2Request::process(ICanChannel& channel, size_t timeout, size_t sendMessagesDelay)
{
    const uint8_t ecuId = _message.getEcuId();
    const auto requestId = _message.getRequestId();

    const uint32_t canId = 0xFFFFE;
    for (const auto& frameData : _message.getFrames()) {
        if (!channel.send({canId, {frameData.begin(), frameData.end()}, true})) {
            throw std::runtime_error("Failed to send CAN message");
        }
        if (sendMessagesDelay > 0) {
            std::this_thread::sleep_for(std::chrono::milliseconds(sendMessagesDelay));
        }
    }

    bool firstMessage = true;
    bool inSeries = false;
    uint8_t seriesId = 0x09;
    const auto restRequestSize{requestId.size() - 1};
    std::vector<uint8_t> result;
    while (true) {
        CanFrame response;
        if (!channel.receive(response, static_cast<unsigned long>(timeout))) {
            throw std::runtime_error("Failed to receive response");
        }
        if (response.data.empty()) {
            continue;
        }
        size_t dataOffset = 0;
        if (firstMessage) {
            checkD2Error(ecuId, requestId, response.data.data(), response.data.size());
            dataOffset = 3;
            if (response.data.size() < dataOffset + restRequestSize + 1) {
                continue;
            }
            const auto acceptMessage{(response.data[1] == ecuId) && (response.data[2] == requestId[0] + 0x40)};
            if (!acceptMessage || !std::equal(requestId.begin() + 1, requestId.end(), response.data.begin() + dataOffset)) {
                continue;
            }
            inSeries = !(response.data[0] & 0x40);
            firstMessage = false;
            dataOffset += restRequestSize;
        }
        else if (inSeries) {
            if (response.data.size() < 1) {
                continue;
            }
            dataOffset = 1;
            const uint8_t header{response.data[0]};
            if (header & 0x40) {
                if (header < 0x48) {
                    throw std::runtime_error("Wrong data length in series");
                }
                inSeries = false;
            }
        }
        result.reserve(result.size() + response.data.size() - dataOffset);
        std::copy(response.data.cbegin() + dataOffset, response.data.cend(), std::back_inserter(result));
        if (!inSeries) {
            break;
        }
    }
    return result;
}

} // namespace common
