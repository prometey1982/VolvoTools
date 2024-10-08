#include "CanMessage.hpp"

#include <algorithm>
#include <array>
#include <iterator>
#include <stdexcept>

namespace common {

CanMessage::CanMessage(uint32_t canId, unsigned long protocolId, const std::vector<DataType> &data)
    : _canId{canId}
    , _protocolId{protocolId}
    , _data{data}
{}

CanMessage::CanMessage(uint32_t canId, unsigned long protocolId, std::vector<DataType> &&data) noexcept
    : CanMessage{canId, protocolId, data}
{}

const std::vector<CanMessage::DataType> &CanMessage::data() const {
  return _data;
}

uint32_t CanMessage::getCanId() const {
  return _canId;
}

} // namespace common
