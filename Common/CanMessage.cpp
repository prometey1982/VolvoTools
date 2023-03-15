#include "CanMessage.hpp"

#include <algorithm>
#include <array>
#include <iterator>
#include <stdexcept>

namespace common {

CanMessage::CanMessage(const std::vector<DataType> &data)
    : _data{data} {}

const std::vector<CanMessage::DataType>& CanMessage::data() const { return _data; }

} // namespace common
