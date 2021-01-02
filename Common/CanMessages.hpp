#pragma once

#include "CEMCanMessage.hpp"

#include <vector>

namespace common {

struct CanMessages {
  static CEMCanMessage createWriteOffsetMsg(uint32_t offset);
  static std::vector<PASSTHRU_MSG>
  createWriteDataMsgs(const std::vector<uint8_t> &bin,
                      unsigned long protocolId, unsigned long flags);
  static std::vector<PASSTHRU_MSG>
  createWriteDataMsgs(const std::vector<uint8_t> &bin,
                      size_t beginOffset, size_t endOffset,
                      unsigned long protocolId, unsigned long flags);

  static const CEMCanMessage wakeUpECM;
  static const CEMCanMessage preFlashECMMsg;
  static const CEMCanMessage restartECMMsg;
  static const CEMCanMessage requestVIN;
  static const CEMCanMessage requestMemory;
  static const CEMCanMessage unregisterAllMemoryRequest;
  static const CEMCanMessage wakeUpCanRequest;
  static const CEMCanMessage goToSleepCanRequest;

  static const std::vector<uint8_t> me7BootLoader;
  static const std::vector<uint8_t> me9BootLoader;
};

} // namespace common
