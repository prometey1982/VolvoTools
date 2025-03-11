#include "common/D2Message.hpp"

#include <algorithm>
#include <array>
#include <iterator>
#include <stdexcept>

namespace {
const std::vector<uint8_t> D2MessagePrefix{0x00, 0x0F, 0xFF, 0xFE};

static std::vector<std::array<uint8_t, common::D2Message::CanPayloadSize>>
generateCANProtocolMessages(const std::vector<uint8_t> &data) {
  std::vector<std::array<uint8_t, common::D2Message::CanPayloadSize>> result;
  const size_t maxSingleMessagePayload = 7u;
  const bool isMultipleMessages = data.size() > maxSingleMessagePayload;
  uint8_t messagePrefix = isMultipleMessages ? 0x88 : 0xC8;
  for (size_t i = 0; i < data.size(); i += maxSingleMessagePayload) {
    const auto payloadSize = static_cast<uint8_t>(
        std::min(data.size() - i, maxSingleMessagePayload));
    uint8_t newPrefix = messagePrefix + payloadSize;
    std::array<uint8_t, 8> canPayload;
    canPayload[0] = newPrefix;
    memset(&canPayload[1], 0, canPayload.size() - 1);
    memcpy(&canPayload[1], data.data() + i, payloadSize);
    result.emplace_back(std::move(canPayload));
    messagePrefix = 0x48;
  }
  return result;
}

uint8_t getData(uint8_t ecuId, const std::vector<uint8_t>& data1, const std::vector<uint8_t>& data2, size_t offset) {
  if(offset == 0)
    return ecuId;
  --offset;
  return offset < data1.size() ? data1[offset] : data2[offset - data1.size()];
}

static std::vector<std::array<uint8_t, common::D2Message::CanPayloadSize>>
generateCANProtocolMessages(uint8_t ecuId, const std::vector<uint8_t>& requestId, const std::vector<uint8_t> &params) {
  std::vector<std::array<uint8_t, common::D2Message::CanPayloadSize>> result;
  const size_t maxSingleMessagePayload = 7u;
  const bool isMultipleMessages = requestId.size() + params.size() > maxSingleMessagePayload;
  uint8_t messagePrefix = isMultipleMessages ? 0x88 : 0xC8;
  uint8_t seriesId = 0x08;
  const auto dataSize = requestId.size() + params.size() + 1;
  for (size_t i = 0; i < dataSize; i += maxSingleMessagePayload) {
    const auto payloadSize =
        std::min(dataSize - i, maxSingleMessagePayload);
    bool inSeries = isMultipleMessages && (i + payloadSize < dataSize);
    seriesId = ((seriesId - 8) + 1) % 8 + 8;
    uint8_t newPrefix = inSeries? seriesId : messagePrefix + payloadSize;
    std::array<uint8_t, 8> canPayload;
    canPayload[0] = newPrefix;
    memset(&canPayload[1], 0, canPayload.size() - 1);
    for(size_t j = 0; j < payloadSize; ++j) {
        canPayload[j + 1] = getData(ecuId, requestId, params, i + j);
    }
    result.emplace_back(std::move(canPayload));
    messagePrefix = 0x48;
  }
  return result;
}
} // namespace

namespace common {

/*static*/ uint8_t D2Message::getECUType(const uint8_t *const buffer) {
  if (buffer[0] == 0x01 && buffer[1] == 0x20 && buffer[2] == 0x00 &&
      buffer[3] == 0x05)
    return static_cast<uint8_t>(ECUType::TCM);
  else if (buffer[0] == 0x01 && buffer[1] == 0x20 && buffer[2] == 0x00 &&
           buffer[3] == 0x21)
    return static_cast<uint8_t>(ECUType::ECM_ME);
  return static_cast<uint8_t>(ECUType::CEM);
}

/*static*/ uint8_t D2Message::getECUType(const std::vector<uint8_t> &buffer) {
  return getECUType(buffer.data());
}
#if 0
D2Message D2Message::makeD2Message(uint8_t ecuId,
                                   std::vector<uint8_t> request) {
  const uint8_t payloadLength = 1 + static_cast<uint8_t>(request.size());
  request.insert(request.begin(), ecuId);
  return D2Message(request);
}

D2Message::D2Message(const std::vector<uint8_t> &data)
    : CanMessage{0xFFFFE, std::move(generateCANProtocolMessages(data))} {}
#endif
D2Message D2Message::makeD2RawMessage(uint8_t ecuId,
                                      const std::vector<uint8_t> &request) {
  DataType data;
  memset(data.data(), 0, data.size());
  data[0] = ecuId;
  const auto payloadLength = request.size();
  if (payloadLength >= data.size())
    throw std::runtime_error("Raw message has length >= 8");
  std::copy(request.begin(), request.end(), data.begin() + 1);
  return D2Message({data});
}

D2Message::D2Message(const std::vector<DataType> &data) : CanMessage{0xFFFFE, data}, _ecuId{} {}

D2Message::D2Message(std::vector<DataType> &&data)
    : CanMessage{0xFFFFE, std::move(data)}, _ecuId{} {}

D2Message::D2Message(uint8_t ecuId, const std::vector<uint8_t>& requestId, const std::vector<uint8_t>& params)
    : CanMessage{0xFFFFE, std::move(generateCANProtocolMessages(ecuId, requestId, params))}
    , _ecuId{ecuId}
    , _requestId{requestId} {}

uint8_t D2Message::getEcuId() const {
  return _ecuId;
}

const std::vector<uint8_t>& D2Message::getRequestId() const {
  return _requestId;
}

} // namespace common
