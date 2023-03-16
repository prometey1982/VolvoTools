#pragma once

#include <array>
#include <cstdint>
#include <vector>

#include "../j2534/BaseMessage.hpp"

namespace common {

class CanMessage: public j2534::BaseMessage {
public:
  static constexpr size_t CanPayloadSize = 8;
  using DataType = std::array<uint8_t, CanPayloadSize>;

protected:
  explicit CanMessage(const std::vector<DataType> &data);
  explicit CanMessage(std::vector<DataType> &&data);

  const std::vector<DataType>& data() const;

private:
  const std::vector<DataType> _data;
};

}
