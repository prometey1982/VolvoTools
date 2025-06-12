#pragma once

#include <array>
#include <cstdint>
#include <vector>

#include <j2534/BaseMessage.hpp>

namespace common {

class UDSMessage : public j2534::BaseMessage {
public:
  explicit UDSMessage(uint32_t canId, const std::vector<uint8_t>& data);
  explicit UDSMessage(uint32_t canId, std::vector<uint8_t>&& data) noexcept;

  UDSMessage(const UDSMessage&) = default;
  UDSMessage(UDSMessage&&) = default;

  const UDSMessage& operator=(const UDSMessage& rhs);

  virtual std::vector<PASSTHRU_MSG>
      toPassThruMsgs(unsigned long ProtocolID, unsigned long Flags) const override;

protected:
  const std::vector<uint8_t> &data() const;

private:
  std::vector<uint8_t> _data;
};

} // namespace common
