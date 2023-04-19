#include "D2Message.hpp"

#include <algorithm>
#include <array>
#include <iterator>
#include <stdexcept>

namespace {
const std::vector<uint8_t> D2MessagePrefix{0x00, 0x0F, 0xFF, 0xFE};

PASSTHRU_MSG toPassThruMsg(const uint8_t *Data, size_t DataSize,
                           unsigned long ProtocolID, unsigned long Flags) {
  PASSTHRU_MSG result;
  result.ProtocolID = ProtocolID;
  result.RxStatus = 0;
  result.TxFlags = Flags;
  result.Timestamp = 0;
  result.ExtraDataIndex = 0;
  result.DataSize = DataSize + D2MessagePrefix.size();
  memcpy(result.Data, D2MessagePrefix.data(), D2MessagePrefix.size());
  memcpy(result.Data + D2MessagePrefix.size(), Data, DataSize);
  return result;
}

static std::vector<std::array<uint8_t, common::D2Message::CanPayloadSize>>
generateCANProtocolMessages(const std::vector<uint8_t> &data) {
  std::vector<std::array<uint8_t, common::D2Message::CanPayloadSize>> result;
  const auto maxSingleMessagePayload = 7u;
  const bool isMultipleMessages = data.size() > maxSingleMessagePayload;
  uint8_t messagePrefix = isMultipleMessages ? 0x88 : 0xC8;
  for (size_t i = 0; i < data.size(); i += maxSingleMessagePayload) {
    const auto payloadSize = static_cast<uint8_t>(
        std::min(data.size() - i, maxSingleMessagePayload));
    const bool isLastMessage = payloadSize <= maxSingleMessagePayload;
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
} // namespace

namespace common {

/*static*/ ECUType D2Message::getECUType(const uint8_t *const buffer) {
  if (buffer[0] == 0x01 && buffer[1] == 0x20 && buffer[2] == 0x00 &&
      buffer[3] == 0x05)
    return ECUType::TCM;
  else if (buffer[0] == 0x01 && buffer[1] == 0x20 && buffer[2] == 0x00 &&
           buffer[3] == 0x21)
    return ECUType::ECM_ME;
  return ECUType::CEM;
}

/*static*/ ECUType D2Message::getECUType(const std::vector<uint8_t> &buffer) {
  return getECUType(buffer.data());
}

D2Message D2Message::makeD2Message(common::ECUType ecuType,
                                   std::vector<uint8_t> request) {
  const uint8_t payloadLength = 1 + static_cast<uint8_t>(request.size());
  request.insert(request.begin(), static_cast<uint8_t>(ecuType));
  return D2Message(request);
}

D2Message D2Message::makeD2RawMessage(uint8_t ecuType,
                                      const std::vector<uint8_t> &request) {
  DataType data;
  memset(data.data(), 0, data.size());
  data[0] = ecuType;
  const auto payloadLength = request.size();
  if (payloadLength >= data.size())
    throw std::runtime_error("Raw message has length >= 8");
  std::copy(request.begin(), request.end(), data.begin() + 1);
  return D2Message({data});
}

D2Message::D2Message(const std::vector<DataType> &data) : CanMessage{data} {}

D2Message::D2Message(std::vector<DataType> &&data)
    : CanMessage{std::move(data)} {}

D2Message::D2Message(const std::vector<uint8_t> &data)
    : CanMessage{std::move(generateCANProtocolMessages(data))} {}

std::vector<PASSTHRU_MSG> D2Message::toPassThruMsgs(unsigned long ProtocolID,
                                                    unsigned long Flags) const {
  std::vector<PASSTHRU_MSG> result;
  for (size_t i = 0; i < data().size(); ++i) {
    result.emplace_back(std::move(::toPassThruMsg(
        data()[i].data(), data()[i].size(), ProtocolID, Flags)));
  }
  return result;
}

} // namespace common
