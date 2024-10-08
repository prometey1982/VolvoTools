#pragma once

#include <array>
#include <cstdint>
#include <vector>

#include "../j2534/BaseMessage.hpp"

namespace common {

class CanMessage : public j2534::BaseMessage {
public:
  static constexpr size_t CanPayloadSize = 8;
  using DataType = std::array<uint8_t, CanPayloadSize>;

protected:
  explicit CanMessage(uint32_t canId, unsigned long protocolId, const std::vector<DataType> &data);
  explicit CanMessage(uint32_t canId, unsigned long protocolId, std::vector<DataType> &&data) noexcept;

  const std::vector<DataType> &data() const;

  uint32_t getCanId() const;

private:
  uint32_t _canId;
  unsigned long _protocolId;
  const std::vector<DataType> _data;
};

} // namespace common
