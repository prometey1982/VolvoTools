#include "common/CanMessage.hpp"

#include <algorithm>
#include <array>
#include <iterator>
#include <stdexcept>

namespace common {

namespace {

PASSTHRU_MSG toPassThruMsg(uint32_t msgId, const uint8_t* Data, size_t DataSize,
    unsigned long ProtocolID, unsigned long Flags) {
    PASSTHRU_MSG result;
    memset(&result, 0, sizeof(result));
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

CanMessage::CanMessage(uint32_t canId, const std::vector<DataType> &data)
    : BaseMessage{ canId }
    , _data{ data }
{}

CanMessage::CanMessage(uint32_t canId, std::vector<DataType> &&data) noexcept
    : BaseMessage{ canId }
    , _data{ std::move(data) }
{}

CanMessage::CanMessage(uint32_t canId, const DataType& data)
    : BaseMessage{ canId }
    , _data{ data }
{
}

const std::vector<CanMessage::DataType> &CanMessage::data() const {
  return _data;
}

std::vector<PASSTHRU_MSG> CanMessage::toPassThruMsgs(unsigned long ProtocolID,
    unsigned long Flags) const {
    std::vector<PASSTHRU_MSG> result;

    for (size_t i = 0; i < data().size(); ++i) {
        result.emplace_back(std::move(toPassThruMsg(getCanId(),
            data()[i].data(), data()[i].size(), ProtocolID, Flags)));
    }
    return result;
}



} // namespace common
