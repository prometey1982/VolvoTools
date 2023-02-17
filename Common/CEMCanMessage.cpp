#include "CEMCanMessage.hpp"

#include <algorithm>
#include <array>
#include <iterator>
#include <stdexcept>

namespace {
const std::vector<uint8_t> CEMMessagePrefix{0x00, 0x0F, 0xFF, 0xFE};

PASSTHRU_MSG toPassThruMsg(const uint8_t* Data, size_t DataSize,
                           unsigned long ProtocolID, unsigned long Flags) {
  PASSTHRU_MSG result;
  result.ProtocolID = ProtocolID;
  result.RxStatus = 0;
  result.TxFlags = Flags;
  result.Timestamp = 0;
  result.ExtraDataIndex = 0;
  result.DataSize = DataSize;
  std::copy(Data, Data + DataSize, result.Data);
  return result;
}

static std::vector<std::array<uint8_t, common::CEMCanMessage::CanPayloadSize>>
generateCANProtocolMessages(const std::vector<uint8_t> &data) {
    std::vector<std::array<uint8_t, common::CEMCanMessage::CanPayloadSize>> result;
    const auto maxSingleMessagePayload = 7u;
    const bool isMultipleMessages = data.size() > maxSingleMessagePayload;
    uint8_t messagePrefix = isMultipleMessages ? 0x80 : 0xC8;
    for(size_t i = 0; i < data.size(); i += maxSingleMessagePayload) {
        const auto payloadSize = static_cast<uint8_t>(std::min(data.size() - i, maxSingleMessagePayload));
        const bool isLastMessage = payloadSize <= maxSingleMessagePayload;
        uint8_t newPrefix = messagePrefix + payloadSize;
        std::array<uint8_t, 8> canPayload;
        canPayload[0] = newPrefix;
        memcpy(&canPayload[1], data.data() + i, payloadSize);
        result.emplace_back(std::move(canPayload));
        messagePrefix = 0x48;
    }
    return result;
}
} // namespace

namespace common {

/*static*/ ECUType CEMCanMessage::getECUType(const uint8_t *const buffer) {
  if (buffer[0] == 0x01 && buffer[1] == 0x20 && buffer[2] == 0x00 &&
      buffer[3] == 0x05)
    return ECUType::TCM;
  else if (buffer[0] == 0x01 && buffer[1] == 0x20 && buffer[2] == 0x00 &&
           buffer[3] == 0x21)
    return ECUType::ECM_ME;
  return ECUType::CEM;
}

/*static*/ ECUType
CEMCanMessage::getECUType(const std::vector<uint8_t> &buffer) {
  return getECUType(buffer.data());
}

CEMCanMessage CEMCanMessage::makeCanMessage(common::ECUType ecuType,
                                            std::vector<uint8_t> request) {
  const uint8_t payloadLength = 1 + static_cast<uint8_t>(request.size());
//  const uint8_t requestLength =
//      (payloadLength > 8 ? 0x80 : 0xC8) + payloadLength;
  request.insert(request.begin(), static_cast<uint8_t>(ecuType));
//  request.insert(request.begin(), requestLength);
  return CEMCanMessage(request);
}

CEMCanMessage::CEMCanMessage(const std::vector<uint8_t> &data)
    : _data{std::move(generateCANProtocolMessages(data))} {}

CEMCanMessage::CEMCanMessage(const std::vector<DataType> &data)
    : _data{data} {}

//std::vector<uint8_t> CEMCanMessage::data() const { return _data; }

std::vector<PASSTHRU_MSG>
CEMCanMessage::toPassThruMsgs(unsigned long ProtocolID,
                              unsigned long Flags) const {
  std::vector<PASSTHRU_MSG> result;
  for(size_t i = 0; i < _data.size(); ++i) {
      result.emplace_back(std::move(::toPassThruMsg(_data[i].data(), _data[i].size(), ProtocolID, Flags)));
  }
  return result;
}

} // namespace common
