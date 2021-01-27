#pragma once

#include "CEMCanMessage.hpp"

#include <vector>

namespace common {

struct CanMessages {
  static CEMCanMessage setCurrentTime(uint8_t hours, uint8_t minutes);
  static CEMCanMessage createWriteOffsetMsg(uint32_t offset);
  static CEMCanMessage createReadOffsetMsg(uint32_t offset);
  static CEMCanMessages createWriteDataMsgs(const std::vector<uint8_t> &bin);
  static CEMCanMessages createWriteDataMsgs(const std::vector<uint8_t> &bin,
                                            size_t beginOffset,
                                            size_t endOffset);
  static CEMCanMessage clearDTCMsgs(ECUType ecuType);
  static CEMCanMessage makeRegisterAddrRequest(uint32_t addr,
                                               size_t dataLength);

  static const CEMCanMessage wakeUpECM;
  static const CEMCanMessage preFlashECMMsg;
  static const CEMCanMessage restartECMMsg;
  static const CEMCanMessage requestVIN;
  static const CEMCanMessage requestMemory;
  static const CEMCanMessage unregisterAllMemoryRequest;
  static const CEMCanMessage wakeUpCanRequest;
  static const CEMCanMessage goToSleepCanRequest;
  static const CEMCanMessage afterBootloaderFlash;
  static const CEMCanMessage startTCMAdaptMsg;
  static const CEMCanMessage testMemoryMsg;
  static const CEMCanMessage enableCommunicationMsg;

  static const std::vector<uint8_t> me7BootLoader;
  static const std::vector<uint8_t> me9BootLoader;
};

} // namespace common
