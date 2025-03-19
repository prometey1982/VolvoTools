#pragma once

#include <array>
#include <cstdint>
#include <vector>

#include <j2534/BaseMessage.hpp>

namespace common {

class TP20Message : public j2534::BaseMessage {
public:
  explicit TP20Message(uint32_t canId, const std::vector<uint8_t>& data);
  explicit TP20Message(uint32_t canId, std::vector<uint8_t>&& data) noexcept;

  virtual std::vector<PASSTHRU_MSG>
      toPassThruMsgs(unsigned long ProtocolID, unsigned long Flags) const override;

protected:
  const std::vector<uint8_t> &data() const;

private:
  const std::vector<uint8_t> _data;
};

} // namespace common
