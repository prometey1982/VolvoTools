#include "CEMCanMessage.hpp"

#include <stdexcept>

namespace logger {
static std::vector<uint8_t>
generateCemMessage(const std::vector<uint8_t> &data) {
  // Fill begin of the message with CEM message ID.
  std::vector<uint8_t> result{0x00, 0x0F, 0xFF, 0xFE};
  result.insert(result.end(), data.cbegin(), data.cend());
  if (result.size() <= 12) {
    result.resize(12);
  } else {
    throw std::runtime_error(
        "Unexpected length of CAN message. Should be less or equal to 12");
  }
  return result;
}

CEMCanMessage::CEMCanMessage(const std::vector<uint8_t> &data)
    : _data{generateCemMessage(data)} {}

std::vector<uint8_t> CEMCanMessage::data() const { return _data; }

PASSTHRU_MSG CEMCanMessage::toPassThruMsg(unsigned long ProtocolID,
                                          unsigned long Flags) const {
  PASSTHRU_MSG result;
  result.ProtocolID = ProtocolID;
  result.RxStatus = 0;
  result.TxFlags = Flags;
  result.Timestamp = 0;
  result.ExtraDataIndex = 0;
  result.DataSize = _data.size();
  std::copy(_data.begin(), _data.end(), result.Data);
  return result;
}

} // namespace logger
