#pragma once

#include "common/CanMessage.hpp"

namespace common {

enum class ECUType : uint8_t {
  ADM = 0x0B,
  AEM = 0x52,
  AUD = 0x6D,
  BCM = 0x01,
  CCM = 0x29,
  CEM = 0x50,
  CPM = 0x18,
  DDM = 0x43,
  DEM = 0x1A,
  DIM = 0x51,
  ECM_ME = 0x7A,
  EPS = 0x30,
  GPS = 0x72,
  IAM = 0x75,
  ICM = 0x54,
  KVM = 0x2D,
  MMM = 0x66,
  PAM = 0x63,
  PDM = 0x45,
  PHM = 0x64,
  PSM = 0x2E,
  SRS = 0x58,
  SUB = 0x68,
  SWM = 0x49,
  TCM = 0x6E,
  TMC = 0x73,
  TRM = 0x23,
};

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
