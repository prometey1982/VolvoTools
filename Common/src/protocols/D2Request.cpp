#include "common/protocols/D2Request.hpp"

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

D2Request::D2Request(const D2Message& message)
    : _message{ message }
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
    bool inSeries = false;
    uint8_t seriesId = 0x09;
    const auto restRequestSize{requestId.size() - 1};
    std::vector<uint8_t> result;
    channel.readMsgs([&result, ecuId, requestId, &firstMessage, &inSeries, &seriesId, restRequestSize, this](const uint8_t* data, size_t dataSize) {
        size_t dataOffset = 0;
        if(firstMessage) {
            checkD2Error(ecuId, requestId, data, dataSize);
            dataOffset = 7;
            if(dataSize < dataOffset + restRequestSize + 1) {
                return true;
            }
            const auto acceptMessage{(data[5] == ecuId) && (data[6] == requestId[0] + 0x40)};
            if(!acceptMessage || !std::equal(requestId.begin() + 1, requestId.end(), data + dataOffset)) {
                return true;
            }
            inSeries = !(data[4] & 0x40);
            firstMessage = false;
            dataOffset += restRequestSize;
        }
        else if(inSeries) {
            if(dataSize < 5) {
                return true;
            }
            dataOffset = 5;
            const uint8_t header{data[4]};
            if(header & 0x40) {
                if(header < 0x48) {
                    throw std::runtime_error("Wrong data length in series");
                }
                inSeries = false;
                dataSize = dataOffset + header - 0x48;
            }
            else if(seriesId == header) {
                seriesId = ((seriesId - 8) + 1) % 8 + 8;
            } else {
                throw std::runtime_error("Wrong series index");
            }
        }
        result.reserve(result.size() + dataSize - dataOffset);
        std::copy(data + dataOffset, data + dataSize, std::back_inserter(result));
        return inSeries;
    }, timeout);
    return result;
}

} // namespace common
