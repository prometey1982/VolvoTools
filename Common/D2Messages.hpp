#pragma once

#include "D2Message.hpp"

#include <vector>

namespace common {

struct D2Messages {
  static D2Message setCurrentTime(uint8_t hours, uint8_t minutes);
  static D2Message createSetMemoryAddrMsg(ECUType ecuType,
                                              uint32_t offset);
  static D2Message createCalculateChecksumMsg(ECUType ecuType,
                                                  uint32_t offset);
  static D2Message createReadOffsetMsg2(ECUType ecuType,
                                            uint32_t offset);
  static D2Message createReadDataByOffsetMsg(ECUType ecuType,
                                                 uint32_t offset, uint8_t size);
  static D2Message createReadDataByAddrMsg(common::ECUType ecuType,
                                               uint32_t addr, uint8_t size);
  static std::vector<D2Message> createWriteDataMsgs(ECUType ecuType,
                                            const std::vector<uint8_t> &bin);
  static std::vector<D2Message> createWriteDataMsgs(ECUType ecuType,
                                            const std::vector<uint8_t> &bin,
                                            size_t beginOffset,
                                            size_t endOffset);
  static D2Message createReadTCMDataByAddr(uint32_t addr, size_t dataSize);
  static D2Message createWriteDataByAddrMsg(ECUType ecuType,
                                                uint32_t addr, uint8_t data);
  static D2Message clearDTCMsgs(ECUType ecuType);
  static D2Message makeRegisterAddrRequest(uint32_t addr,
                                               size_t dataLength);
  static D2Message createStartPrimaryBootloaderMsg(ECUType ecuType);
  static D2Message createWakeUpECUMsg(ECUType ecuType);
  static D2Message createJumpToMsg(ECUType ecuType,
                                       uint8_t data1 = 0, uint8_t data2 = 0,
                                       uint8_t data3 = 0, uint8_t data4 = 0,
                                       uint8_t data5 = 0, uint8_t data6 = 0);
  static D2Message createEraseMsg(ECUType ecuType);
  static D2Message createSBLTransferCompleteMsg(ECUType ecuType);

  static const D2Message requestVIN;
  static const D2Message requestVehicleConfiguration;
  static const D2Message requestMemory;
  static const D2Message unregisterAllMemoryRequest;
  static const D2Message wakeUpCanRequest;
  static const D2Message goToSleepCanRequest;
  static const D2Message startTCMAdaptMsg;
  static const D2Message enableCommunicationMsg;

  static const std::vector<uint8_t> me7BootLoader;
  static const std::vector<uint8_t> me9BootLoader;
};

} // namespace common
