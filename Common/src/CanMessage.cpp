#include "common/CanMessage.hpp"

#include <algorithm>
#include <array>
#include <iterator>
#include <stdexcept>

namespace common {

CanMessage::CanMessage(uint32_t canId, const DataType& data)
    : _canId{ canId }
    , _data{ data }
{
}

CanMessage::CanMessage(uint32_t canId, DataType&& data)
    : _canId{ canId }
    , _data{ std::move(data) }
{
}

} // namespace common
