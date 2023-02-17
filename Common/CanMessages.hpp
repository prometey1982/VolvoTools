#pragma once

#include "CEMCanMessage.hpp"

#include <vector>

namespace common {

struct CanMessages {
  static CEMCanMessage setCurrentTime(uint8_t hours, uint8_t minutes);
  static CEMCanMessage createSetMemoryAddrMsg(ECUType ecuType,
                                              uint32_t offset);
  static CEMCanMessage createCalculateChecksumMsg(ECUType ecuType,
                                                  uint32_t offset);
  static CEMCanMessage createReadOffsetMsg2(ECUType ecuType,
                                            uint32_t offset);
  static CEMCanMessage createReadDataByOffsetMsg(ECUType ecuType,
                                                 uint32_t offset, uint8_t size);
  static CEMCanMessage createReadDataByAddrMsg(common::ECUType ecuType,
                                               uint32_t addr, uint8_t size);
  static CEMCanMessage createWriteDataMsgs(ECUType ecuType,
                                            const std::vector<uint8_t> &bin);
  static CEMCanMessage createWriteDataMsgs(ECUType ecuType,
                                            const std::vector<uint8_t> &bin,
                                            size_t beginOffset,
                                            size_t endOffset);
  static CEMCanMessage createReadTCMDataByAddr(uint32_t addr, size_t dataSize);
  static CEMCanMessage createWriteDataByAddrMsg(ECUType ecuType,
                                                uint32_t addr, uint8_t data);
  static CEMCanMessage clearDTCMsgs(ECUType ecuType);
  static CEMCanMessage makeRegisterAddrRequest(uint32_t addr,
                                               size_t dataLength);
  static CEMCanMessage createStartPrimaryBootloaderMsg(ECUType ecuType);
  static CEMCanMessage createWakeUpECUMsg(ECUType ecuType);
  static CEMCanMessage createJumpToMsg(ECUType ecuType,
                                       uint8_t data1 = 0, uint8_t data2 = 0,
                                       uint8_t data3 = 0, uint8_t data4 = 0,
                                       uint8_t data5 = 0, uint8_t data6 = 0);
  static CEMCanMessage createEraseMsg(ECUType ecuType);
  static CEMCanMessage createSBLTransferCompleteMsg(ECUType ecuType);

  static const CEMCanMessage requestVIN;
  static const CEMCanMessage requestVehicleConfiguration;
  static const CEMCanMessage requestMemory;
  static const CEMCanMessage unregisterAllMemoryRequest;
  static const CEMCanMessage wakeUpCanRequest;
  static const CEMCanMessage goToSleepCanRequest;
  static const CEMCanMessage startTCMAdaptMsg;
  static const CEMCanMessage enableCommunicationMsg;

  static const std::vector<uint8_t> me7BootLoader;
  static const std::vector<uint8_t> me9BootLoader;
};

} // namespace common
