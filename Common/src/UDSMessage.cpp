#include "common/UDSMessage.hpp"

#include <algorithm>
#include <array>
#include <iterator>
#include <stdexcept>

namespace common {

namespace {

PASSTHRU_MSG toPassThruMsg(uint32_t msgId, const uint8_t* Data, size_t DataSize,
    unsigned long ProtocolID, unsigned long Flags) {
    PASSTHRU_MSG result;
    result.ProtocolID = ProtocolID;
    result.RxStatus = 0;
    result.TxFlags = Flags;
    result.Timestamp = 0;
    result.ExtraDataIndex = 0;
    result.DataSize = DataSize + sizeof(msgId);
    result.Data[0] = (msgId >> 24) & 0xFF;
    result.Data[1] = (msgId >> 16) & 0xFF;
    result.Data[2] = (msgId >> 8) & 0xFF;
    result.Data[3] = (msgId >> 0) & 0xFF;
    memcpy(result.Data + sizeof(msgId), Data, DataSize);
    return result;
}

}

UDSMessage::UDSMessage(uint32_t canId, const std::vector<uint8_t> &data)
    : BaseMessage{ canId }
    , _data{ data }
{}

UDSMessage::UDSMessage(uint32_t canId, std::vector<uint8_t> &&data) noexcept
    : BaseMessage{ canId }
    , _data{ std::move(data) }
{}

const std::vector<uint8_t> & UDSMessage::data() const {
  return _data;
}

std::vector<PASSTHRU_MSG> UDSMessage::toPassThruMsgs(unsigned long ProtocolID,
    unsigned long Flags) const {
    std::vector<PASSTHRU_MSG> result;

    result.emplace_back(std::move(toPassThruMsg(getCanId(),
        data().data(), data().size(), ProtocolID, Flags | ISO15765_FRAME_PAD)));

    return result;
}

} // namespace common
