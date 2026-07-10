#include "common/protocols/D2Messages.hpp"

#include <algorithm>
#include <iterator>

namespace common {

CanFrame makeRawMessage(const CanMessage::DataType& payload)
{
  return { D2Message::CanId, payload, true };
}

/*static*/ D2Message D2Messages::setCurrentTime(uint8_t hours,
                                                uint8_t minutes) {
  uint32_t value = minutes + hours * 60;
  return D2Message(static_cast<uint8_t>(common::ECUType::DIM),
                   {0xB0, 0x07, 0x01, 0xFF},
                   {static_cast<uint8_t>((value >> 8) & 0xFF),
                    static_cast<uint8_t>(value & 0xFF)});
}

/*static*/ D2Message
D2Messages::createReadDataByOffsetMsg(uint8_t ecuId, uint32_t addr,
                                      uint8_t size) {
  return D2Message(ecuId,
                   {0xA7}, {static_cast<uint8_t>(addr >> 16),
                            static_cast<uint8_t>(addr >> 8),
                            static_cast<uint8_t>(addr), 1, size});
}

/*static*/ D2Message
D2Messages::createReadDataByAddrMsg(uint8_t ecuId, uint32_t addr,
                                    uint8_t size) {
  const uint8_t byte1 = (addr & 0xFF000000) >> 24;
  const uint8_t byte2 = (addr & 0xFF0000) >> 16;
  const uint8_t byte3 = (addr & 0xFF00) >> 8;
  const uint8_t byte4 = (addr & 0xFF);
  return D2Message(ecuId, {0xB4, 21, 34}, {byte1, byte2, byte3,
                                           byte4, size});
}

/*static*/ D2Message D2Messages::createReadTCMTF80DataByAddr(uint32_t addr,
                                                             size_t dataSize) {
  const uint8_t byte1 = (addr & 0xFF000000) >> 24;
  const uint8_t byte2 = (addr & 0xFF0000) >> 16;
  const uint8_t byte3 = (addr & 0xFF00) >> 8;
  const uint8_t byte4 = (addr & 0xFF);
  return D2Message(static_cast<uint8_t>(common::ECUType::TCM),
                   {0xB4, 0x21, 0x34}, {byte1, byte2, byte3,
                                        byte4, static_cast<uint8_t>(dataSize)});
}

///*static*/ D2Message D2Messages::createWriteTCMDataByAddr(uint32_t addr,
//                                                         uint8_t data) {
//  const uint8_t byte1 = (addr & 0xFF000000) >> 24;
//  const uint8_t byte2 = (addr & 0xFF0000) >> 16;
//  const uint8_t byte3 = (addr & 0xFF00) >> 8;
//  const uint8_t byte4 = (addr & 0xFF);
//  return D2Message::makeD2Message(common::ECUType::TCM,
//                                  {0xB4, 0x40, 0x34, byte1, byte2, byte3,
//                                   byte4, data});
//}

/*static*/ D2Message D2Messages::createWriteDataByAddrMsg(uint8_t ecuId,
                                                          uint32_t addr,
                                                          uint8_t data) {
  const uint8_t byte1 = (addr & 0xFF0000) >> 16;
  const uint8_t byte2 = (addr & 0xFF00) >> 8;
  const uint8_t byte3 = (addr & 0xFF);
  return D2Message(ecuId, {0xBA}, {byte1, byte2, byte3,
                                   data});
}

/*static*/ D2Message D2Messages::clearDTCMsgs(uint8_t ecuId) {
  return D2Message(ecuId, {0xAF, 0x11});
}

/*static*/ D2Message D2Messages::makeRegisterAddrRequest(uint32_t addr,
                                                         size_t dataLength) {
  const uint8_t byte1 = (addr & 0xFF0000) >> 16;
  const uint8_t byte2 = (addr & 0xFF00) >> 8;
  const uint8_t byte3 = (addr & 0xFF);
  return D2Message(
      static_cast<uint8_t>(common::ECUType::ECM_ME),
      {0xAA, 0x50}, {byte1, byte2, byte3, static_cast<uint8_t>(dataLength)});
}

const D2Message D2Messages::requestVIN{
    common::D2Message(static_cast<uint8_t>(common::ECUType::CEM), {0xB9, 0xFB})};
const D2Message D2Messages::requestVehicleConfiguration{
    common::D2Message(static_cast<uint8_t>(common::ECUType::CEM), {0xB9, 0xFC})};
const D2Message D2Messages::requestMemory{common::D2Message(
    static_cast<uint8_t>(common::ECUType::ECM_ME), {0xA6, 0xF0, 0x00, 0x01})};
const D2Message D2Messages::unregisterAllMemoryRequest{
    common::D2Message(static_cast<uint8_t>(common::ECUType::ECM_ME), {0xAA, 0x00})};
const D2Message D2Messages::startTCMAdaptMsg{
    common::D2Message(static_cast<uint8_t>(common::ECUType::TCM), {0xB2, 0x50})};
const D2Message D2Messages::enableCommunicationMsg{
    common::D2Message(static_cast<uint8_t>(common::ECUType::CEM), {0xD8})};

/*static*/ CanFrame D2RawMessages::createSetMemoryAddrMsg(uint8_t ecuId,
                                                        uint32_t addr) {
  const uint8_t byte1 = (addr & 0xFF000000) >> 24;
  const uint8_t byte2 = (addr & 0xFF0000) >> 16;
  const uint8_t byte3 = (addr & 0xFF00) >> 8;
  const uint8_t byte4 = (addr & 0xFF);
  return makeRawMessage({ecuId, 0x9C, byte1, byte2, byte3, byte4});
}

/*static*/ CanFrame
D2RawMessages::createCalculateChecksumMsg(uint8_t ecuId, uint32_t addr) {
  const uint8_t byte1 = (addr & 0xFF000000) >> 24;
  const uint8_t byte2 = (addr & 0xFF0000) >> 16;
  const uint8_t byte3 = (addr & 0xFF00) >> 8;
  const uint8_t byte4 = (addr & 0xFF);
  return makeRawMessage({ecuId, 0xB4, byte1, byte2, byte3, byte4});
}

/*static*/ CanFrame D2RawMessages::createReadOffsetMsg2(uint8_t ecuId,
                                                      uint32_t addr) {
  const uint8_t byte1 = (addr & 0xFF000000) >> 24;
  const uint8_t byte2 = (addr & 0xFF0000) >> 16;
  const uint8_t byte3 = (addr & 0xFF00) >> 8;
  const uint8_t byte4 = (addr & 0xFF);
  return makeRawMessage({ecuId, 0xBC, byte1, byte2, byte3, byte4});
}

/*static*/ CanFrame
D2RawMessages::createStartPrimaryBootloaderMsg(uint8_t ecuId) {
  return makeRawMessage({ecuId, 0xC0});
}

/*static*/ CanFrame D2RawMessages::createWakeUpECUMsg(uint8_t ecuId) {
  return makeRawMessage({ecuId, 0xC8});
}

/*static*/ CanFrame D2RawMessages::createJumpToMsg(uint8_t ecuId,
                                                 uint8_t data1, uint8_t data2,
                                                 uint8_t data3, uint8_t data4,
                                                 uint8_t data5, uint8_t data6) {
  return makeRawMessage(
      {ecuId, 0xA0, data1, data2, data3, data4, data5, data6});
}

/*static*/ CanFrame D2RawMessages::createEraseMsg(uint8_t ecuId) {
  return makeRawMessage({ecuId, 0xF8});
}

/*static*/ CanFrame
D2RawMessages::createSBLTransferCompleteMsg(uint8_t ecuId) {
  return makeRawMessage({ecuId, 0xA8});
}

const CanFrame D2RawMessages::wakeUpCanRequest{
    makeRawMessage({0xFF, 0xC8})};
const CanFrame D2RawMessages::goToSleepCanRequest{
    makeRawMessage({0xFF, 0x86})};

} // namespace common
