#include "common/protocols/D2Message.hpp"

#include "common/protocols/D2ECUType.hpp"

#include <algorithm>
#include <array>
#include <iterator>
#include <stdexcept>

namespace {

common::CanMessage::DataType createPayload(const common::CanMessage::DataType& request,
                                           const common::CanMessage::DataType& params)
{
    common::CanMessage::DataType result(request);
    result.insert(result.end(), params.cbegin(), params.cend());
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
    constexpr size_t maxSingleMessagePayload = 7u;
    const auto dataSize = requestId.size() + params.size() + 1;
    uint8_t seriesCounter = 0;
    for (size_t i = 0; i < dataSize; i += maxSingleMessagePayload) {
        const auto payloadSize =
            std::min(dataSize - i, maxSingleMessagePayload);

        const bool firstMessage = (i == 0);
        const bool lastMessage = (i + payloadSize) >= dataSize;

        uint8_t prefix = 8 + (firstMessage ? 0x80 : 0) + (lastMessage ? 0x40 : seriesCounter) + (firstMessage | lastMessage ? payloadSize : 0);

        std::vector<uint8_t> canPayload(8, 0);
        canPayload[0] = prefix;
        for(size_t j = 0; j < payloadSize; ++j) {
            canPayload[j + 1] = getData(ecuId, requestId, params, i + j);
        }
        result.emplace_back(common::D2Message::CanId, std::move(canPayload), true);
        seriesCounter = (seriesCounter + 1) % 8;
    }
    return result;
}

} // namespace

namespace common {

/*static*/ uint8_t D2Message::getECUType(const uint8_t *const buffer)
{
    if (buffer[0] == 0x01 && buffer[1] == 0x20 && buffer[2] == 0x00 &&
        buffer[3] == 0x05)
        return static_cast<uint8_t>(D2ECUType::TCM);
    else if (buffer[0] == 0x01 && buffer[1] == 0x20 && buffer[2] == 0x00 &&
            buffer[3] == 0x21)
        return static_cast<uint8_t>(D2ECUType::ECM_ME);
    return static_cast<uint8_t>(D2ECUType::CEM);
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
