#pragma once

#include "common/CanMessage.hpp"

namespace common {

class D2Message: public CanMessage {
public:
  static constexpr uint32_t CanId = 0xFFFFE;

  static uint8_t getECUType(const uint8_t *const buffer);
  static uint8_t getECUType(const std::vector<uint8_t> &buffer);

//  explicit D2Message(const std::vector<uint8_t> &data);
  explicit D2Message(const DataType &data);
  explicit D2Message(DataType &&data);
  D2Message(D2Message&&) noexcept = default;
  D2Message(const D2Message&) noexcept = default;
  D2Message(uint8_t ecuId, const std::vector<uint8_t>& requestId, const std::vector<uint8_t>& params = {});

  uint8_t getEcuId() const;
  const std::vector<uint8_t>& getRequestId() const;
  virtual std::vector<CanFrame> getFrames() const override;

private:
  const uint8_t _ecuId;
  const std::vector<uint8_t> _requestId;
};

} // namespace common
