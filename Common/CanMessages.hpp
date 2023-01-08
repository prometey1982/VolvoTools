#pragma once

#include "CEMCanMessage.hpp"

#include <vector>

namespace common {

struct CanMessages {
  static CEMCanMessage setCurrentTime(uint8_t hours, uint8_t minutes);
  static CEMCanMessage createSetMemoryAddrMsg(common::ECUType ecuType,
                                              uint32_t offset);
  static CEMCanMessage createCalculateChecksumMsg(common::ECUType ecuType,
                                                  uint32_t offset);
  static CEMCanMessage createReadOffsetMsg2(common::ECUType ecuType,
                                            uint32_t offset);
  static CEMCanMessage createReadDataByOffsetMsg(common::ECUType ecuType,
                                                 uint32_t offset);
  static CEMCanMessage createReadDataByAddrMsg(common::ECUType ecuType,
                                               uint32_t addr, uint8_t size);
  static CEMCanMessages createWriteDataMsgs(common::ECUType ecuType,
                                            const std::vector<uint8_t> &bin);
  static CEMCanMessages createWriteDataMsgs(common::ECUType ecuType,
                                            const std::vector<uint8_t> &bin,
                                            size_t beginOffset,
                                            size_t endOffset);
  static CEMCanMessage createReadTCMDataByAddr(uint32_t addr, size_t dataSize);
  static CEMCanMessage createWriteDataByAddrMsg(common::ECUType ecuType,
                                                uint32_t addr, uint8_t data);
  static CEMCanMessage clearDTCMsgs(ECUType ecuType);
  static CEMCanMessage makeRegisterAddrRequest(uint32_t addr,
                                               size_t dataLength);
  static CEMCanMessage createStartPrimaryBootloaderMsg(common::ECUType ecuType);
  static CEMCanMessage createWakeUpECUMsg(common::ECUType ecuType);
  static CEMCanMessage createJumpToMsg(common::ECUType ecuType);
  static CEMCanMessage createEraseMsg(common::ECUType ecuType);

  static const CEMCanMessage requestVIN;
  static const CEMCanMessage requestMemory;
  static const CEMCanMessage unregisterAllMemoryRequest;
  static const CEMCanMessage wakeUpCanRequest;
  static const CEMCanMessage goToSleepCanRequest;
  static const CEMCanMessage afterBootloaderFlash;
  static const CEMCanMessage startTCMAdaptMsg;
  static const CEMCanMessage enableCommunicationMsg;

  static const std::vector<uint8_t> me7BootLoader;
  static const std::vector<uint8_t> me9BootLoader;
};

} // namespace common
