#pragma once

#include "D2Message.hpp"

#include <vector>

namespace common {

struct D2Messages {
  static D2Message setCurrentTime(uint8_t hours, uint8_t minutes);
  static D2Message createReadDataByOffsetMsg(uint8_t ecuId, uint32_t offset,
                                             uint8_t size);
  static D2Message createReadDataByAddrMsg(uint8_t ecuId,
                                           uint32_t addr, uint8_t size);
  static D2Message createReadTCMTF80DataByAddr(uint32_t addr, size_t dataSize);
  static D2Message createWriteDataByAddrMsg(uint8_t ecuId, uint32_t addr,
                                            uint8_t data);
  static D2Message clearDTCMsgs(uint8_t ecuId);
  static D2Message makeRegisterAddrRequest(uint32_t addr, size_t dataLength);

  static const D2Message requestVIN;
  static const D2Message requestVehicleConfiguration;
  static const D2Message requestMemory;
  static const D2Message unregisterAllMemoryRequest;
  static const D2Message startTCMAdaptMsg;
  static const D2Message enableCommunicationMsg;
};

struct D2RawMessages {
  static CanFrame createSetMemoryAddrMsg(uint8_t ecuId, uint32_t offset);
  static CanFrame createCalculateChecksumMsg(uint8_t ecuId, uint32_t offset);
  static CanFrame createReadOffsetMsg2(uint8_t ecuId, uint32_t offset);
  static CanFrame createStartPrimaryBootloaderMsg(uint8_t ecuId);
  static CanFrame createWakeUpECUMsg(uint8_t ecuId);
  static CanFrame createJumpToMsg(uint8_t ecuId, uint8_t data1 = 0,
                                  uint8_t data2 = 0, uint8_t data3 = 0,
                                  uint8_t data4 = 0, uint8_t data5 = 0,
                                  uint8_t data6 = 0);
  static CanFrame createEraseMsg(uint8_t ecuId);
  static CanFrame createSBLTransferCompleteMsg(uint8_t ecuId);

  static const CanFrame wakeUpCanRequest;
  static const CanFrame goToSleepCanRequest;
};

} // namespace common
