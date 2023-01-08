#include "CEMCanMessage.hpp"

#include <iterator>
#include <stdexcept>

namespace {
const std::vector<uint8_t> CEMMessagePrefix{0x00, 0x0F, 0xFF, 0xFE};

PASSTHRU_MSG toPassThruMsg(const std::vector<uint8_t> &data,
                           unsigned long ProtocolID, unsigned long Flags) {
  PASSTHRU_MSG result;
  result.ProtocolID = ProtocolID;
  result.RxStatus = 0;
  result.TxFlags = Flags;
  result.Timestamp = 0;
  result.ExtraDataIndex = 0;
  result.DataSize = data.size();
  std::copy(data.begin(), data.end(), result.Data);
  return result;
}

static std::vector<uint8_t>
generateCemMessage(const std::vector<uint8_t> &data) {
  // Fill begin of the message with CEM message ID.
  std::vector<uint8_t> result{CEMMessagePrefix};
  result.insert(result.end(), data.cbegin(), data.cend());
  if (result.size() <= 12) {
    result.resize(12);
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
  const uint8_t requestLength =
      (payloadLength > 8 ? 0x80 : 0xC8) + payloadLength;
  request.insert(request.begin(), static_cast<uint8_t>(ecuType));
  request.insert(request.begin(), requestLength);
  return CEMCanMessage(request);
}

CEMCanMessage::CEMCanMessage(const std::vector<uint8_t> &data)
    : _data{generateCemMessage(data)} {}

CEMCanMessage::CEMCanMessage(const std::vector<uint8_t> &data, bool)
    : _data{data} {}

std::vector<uint8_t> CEMCanMessage::data() const { return _data; }

std::vector<PASSTHRU_MSG>
CEMCanMessage::toPassThruMsgs(unsigned long ProtocolID,
                              unsigned long Flags) const {
  return {toPassThruMsg(ProtocolID, Flags)};
}

PASSTHRU_MSG CEMCanMessage::toPassThruMsg(unsigned long ProtocolID,
                                          unsigned long Flags) const {
  return ::toPassThruMsg(_data, ProtocolID, Flags);
}

CEMCanMessages::CEMCanMessages(
    const std::vector<std::vector<uint8_t>> &messages)
    : _messages{messages} {}

std::vector<PASSTHRU_MSG>
CEMCanMessages::toPassThruMsgs(unsigned long ProtocolID,
                               unsigned long Flags) const {
  std::vector<PASSTHRU_MSG> result;
  std::vector<uint8_t> buffer;
  buffer.reserve(4128);
  buffer = CEMMessagePrefix;
  bool emptyBuffer = true;
  for (const auto &msg : _messages) {
    std::copy(msg.begin(), msg.end(), std::back_inserter(buffer));
    emptyBuffer = false;
    if (buffer.size() >= 4100) {
      result.emplace_back(toPassThruMsg(buffer, ProtocolID, Flags));
      buffer = CEMMessagePrefix;
      emptyBuffer = true;
    }
  }
  if (!emptyBuffer) {
    result.emplace_back(toPassThruMsg(buffer, ProtocolID, Flags));
  }
  return result;
}

} // namespace common
