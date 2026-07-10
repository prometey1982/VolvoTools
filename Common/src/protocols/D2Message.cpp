#include "common/protocols/D2Message.hpp"

#include <algorithm>
#include <array>
#include <iterator>
#include <stdexcept>

namespace {

common::CanMessage::DataType createPayload(const common::CanMessage::DataType& request,
                                           const common::CanMessage::DataType& params)
{
    common::CanMessage::DataType result(request);
    result.insert(result.begin(), params.cbegin(), params.cend());
    return result;
}

uint8_t getData(uint8_t ecuId, const std::vector<uint8_t>& data1, const std::vector<uint8_t>& data2, size_t offset)
{
    if(offset == 0)
        return ecuId;
    --offset;
    return offset < data1.size() ? data1[offset] : data2[offset - data1.size()];
}

std::vector<common::CanFrame>
generateCanFrames(uint8_t ecuId, const std::vector<uint8_t>& requestId, const std::vector<uint8_t> &params)
{
    std::vector<common::CanFrame> result;
    const size_t maxSingleMessagePayload = 7u;
    const bool isMultipleMessages = requestId.size() + params.size() > maxSingleMessagePayload;
    uint8_t messagePrefix = isMultipleMessages ? 0x88 : 0xC8;
    uint8_t seriesId = 0x08;
    bool inSeries = 0;
    const auto dataSize = requestId.size() + params.size() + 1;
    for (size_t i = 0; i < dataSize; i += maxSingleMessagePayload) {
        const auto payloadSize =
            std::min(dataSize - i, maxSingleMessagePayload);
        seriesId = ((seriesId - 8) + 1) % 8 + 8;
        // Last message
        if(i + payloadSize >= dataSize) {
            inSeries = false;
        }
        uint8_t newPrefix = inSeries? seriesId : messagePrefix + payloadSize;
        inSeries = isMultipleMessages && (i + payloadSize < dataSize);
        std::vector<uint8_t> canPayload(common::CanMessage::CanPayloadSize);
        canPayload[0] = newPrefix;
        memset(&canPayload[1], 0, canPayload.size() - 1);
        for(size_t j = 0; j < payloadSize; ++j) {
            canPayload[j + 1] = getData(ecuId, requestId, params, i + j);
        }
        result.emplace_back(common::D2Message::CanId, move(canPayload), true);
        messagePrefix = 0x48;
    }
    return result;
}

} // namespace

namespace common {

/*static*/ uint8_t D2Message::getECUType(const uint8_t *const buffer)
{
    if (buffer[0] == 0x01 && buffer[1] == 0x20 && buffer[2] == 0x00 &&
        buffer[3] == 0x05)
        return static_cast<uint8_t>(ECUType::TCM);
    else if (buffer[0] == 0x01 && buffer[1] == 0x20 && buffer[2] == 0x00 &&
            buffer[3] == 0x21)
        return static_cast<uint8_t>(ECUType::ECM_ME);
    return static_cast<uint8_t>(ECUType::CEM);
}

/*static*/ uint8_t D2Message::getECUType(const std::vector<uint8_t> &buffer)
{
    return getECUType(buffer.data());
}

D2Message::D2Message(const DataType &data) : CanMessage{CanId, data}, _ecuId{} {}

D2Message::D2Message(DataType &&data)
    : CanMessage{CanId, data}, _ecuId{}
    , _requestId{ std::move(data) }
{
}

D2Message::D2Message(uint8_t ecuId, const std::vector<uint8_t>& requestId, const std::vector<uint8_t>& params)
    : CanMessage{CanId, std::move(createPayload(requestId, params))}
    , _ecuId{ecuId}
    , _requestId{requestId}
{
}

uint8_t D2Message::getEcuId() const
{
    return _ecuId;
}

const std::vector<uint8_t>& D2Message::getRequestId() const
{
    return _requestId;
}

std::vector<CanFrame> D2Message::getFrames() const
{
    return generateCanFrames(_ecuId, getData(), {});
}

} // namespace common
