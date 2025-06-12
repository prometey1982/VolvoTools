#pragma once

#include <array>
#include <cstdint>
#include <vector>

#include <j2534/BaseMessage.hpp>

namespace common {

class CanMessage : public j2534::BaseMessage {
public:
  static constexpr size_t CanPayloadSize = 8;
  using DataType = std::array<uint8_t, CanPayloadSize>;

  explicit CanMessage(uint32_t canId, const std::vector<DataType>& data);
  explicit CanMessage(uint32_t canId, std::vector<DataType>&& data) noexcept;
  explicit CanMessage(uint32_t canId, const DataType& data);
  explicit CanMessage(uint32_t canId, DataType&& data);
  CanMessage(CanMessage&&) noexcept = default;
  CanMessage(const CanMessage&) noexcept = default;

  virtual std::vector<PASSTHRU_MSG>
      toPassThruMsgs(unsigned long ProtocolID, unsigned long Flags) const override;

protected:
  const std::vector<DataType> &data() const;

private:
  const std::vector<DataType> _data;
};

} // namespace common
