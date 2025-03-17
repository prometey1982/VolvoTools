#include "common/protocols/D2Messages.hpp"

#include <algorithm>
#include <iterator>

namespace common {

/*static*/ D2Message D2Messages::setCurrentTime(uint8_t hours,
                                                uint8_t minutes) {
  uint32_t value = minutes + hours * 60;
  return D2Message(static_cast<uint8_t>(common::ECUType::DIM),
                   {0xB0, 0x07, 0x01, 0xFF},
                   {static_cast<uint8_t>((value >> 8) & 0xFF),
                    static_cast<uint8_t>(value & 0xFF)});
}

/*static*/ D2Message D2Messages::createSetMemoryAddrMsg(uint8_t ecuId,
                                                        uint32_t addr) {
  const uint8_t byte1 = (addr & 0xFF000000) >> 24;
  const uint8_t byte2 = (addr & 0xFF0000) >> 16;
  const uint8_t byte3 = (addr & 0xFF00) >> 8;
  const uint8_t byte4 = (addr & 0xFF);
  return D2Message::makeD2RawMessage(ecuId,
                                     {0x9C, byte1, byte2, byte3, byte4});
}

/*static*/ D2Message
D2Messages::createCalculateChecksumMsg(uint8_t ecuId, uint32_t addr) {
  const uint8_t byte1 = (addr & 0xFF000000) >> 24;
  const uint8_t byte2 = (addr & 0xFF0000) >> 16;
  const uint8_t byte3 = (addr & 0xFF00) >> 8;
  const uint8_t byte4 = (addr & 0xFF);
  return D2Message::makeD2RawMessage(ecuId,
                                     {0xB4, byte1, byte2, byte3, byte4});
}

/*static*/ D2Message D2Messages::createReadOffsetMsg2(uint8_t ecuId,
                                                      uint32_t addr) {
  const uint8_t byte1 = (addr & 0xFF000000) >> 24;
  const uint8_t byte2 = (addr & 0xFF0000) >> 16;
  const uint8_t byte3 = (addr & 0xFF00) >> 8;
  const uint8_t byte4 = (addr & 0xFF);
  return D2Message::makeD2RawMessage(ecuId, {0xBC, byte1, byte2, byte3, byte4});
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

/*static*/ std::vector<D2Message>
D2Messages::createWriteDataMsgs(uint8_t ecuId,
                                const std::vector<uint8_t> &bin) {
  return createWriteDataMsgs(ecuId, bin, 0, bin.size());
}

/*static*/ std::vector<D2Message>
D2Messages::createWriteDataMsgs(uint8_t ecuId,
                                const std::vector<uint8_t> &bin,
                                size_t beginOffset, size_t endOffset) {
  const auto MaxMessagesPerMessage = 10;
  std::vector<D2Message> result;
  std::vector<D2Message::DataType> resultPayload;
  const size_t chunkSize = 6u;
  for (size_t i = beginOffset; i < endOffset; i += chunkSize) {
    D2Message::DataType payload;
    memset(payload.data(), 0, payload.size());
    auto payloadSize = std::min(chunkSize, endOffset - i);
    uint8_t command = 0xA8 + static_cast<uint8_t>(payloadSize);
    payload[0] = ecuId;
    payload[1] = command;
    std::copy(bin.begin() + i, bin.begin() + i + payloadSize,
              payload.begin() + 2);
    resultPayload.emplace_back(std::move(payload));
    if (resultPayload.size() >= MaxMessagesPerMessage) {
      result.emplace_back(D2Message(std::move(resultPayload)));
      resultPayload.clear();
    }
  }
  D2Message::DataType payload;
  memset(payload.data(), 0, payload.size());
  payload[0] = ecuId;
  payload[1] = 0xA8;
  resultPayload.emplace_back(std::move(payload));
  if (!resultPayload.empty())
    result.emplace_back(D2Message(std::move(resultPayload)));
  return result;
}

/*static*/ D2Message D2Messages::createReadTCMDataByAddr(uint32_t addr,
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

/*static*/ D2Message
D2Messages::createStartPrimaryBootloaderMsg(uint8_t ecuId) {
  return common::D2Message::makeD2RawMessage(ecuId, {0xC0});
}

/*static*/ D2Message D2Messages::createWakeUpECUMsg(uint8_t ecuId) {
  return common::D2Message::makeD2RawMessage(ecuId, {0xC8});
}

/*static*/ D2Message D2Messages::createJumpToMsg(uint8_t ecuId,
                                                 uint8_t data1, uint8_t data2,
                                                 uint8_t data3, uint8_t data4,
                                                 uint8_t data5, uint8_t data6) {
  return common::D2Message::makeD2RawMessage(
      ecuId, {0xA0, data1, data2, data3, data4, data5, data6});
}

/*static*/ D2Message D2Messages::createEraseMsg(uint8_t ecuId) {
  return common::D2Message::makeD2RawMessage(ecuId, {0xF8});
}

/*static*/ D2Message
D2Messages::createSBLTransferCompleteMsg(uint8_t ecuId) {
  return common::D2Message::makeD2RawMessage(ecuId, {0xA8});
}

const D2Message D2Messages::requestVIN{
    common::D2Message(static_cast<uint8_t>(common::ECUType::CEM), {0xB9, 0xFB})};
const D2Message D2Messages::requestVehicleConfiguration{
    common::D2Message(static_cast<uint8_t>(common::ECUType::CEM), {0xB9, 0xFC})};
const D2Message D2Messages::requestMemory{common::D2Message(
    static_cast<uint8_t>(common::ECUType::ECM_ME), {0xA6, 0xF0, 0x00, 0x01})};
const D2Message D2Messages::unregisterAllMemoryRequest{
    common::D2Message(static_cast<uint8_t>(common::ECUType::ECM_ME), {0xAA, 0x00})};
const D2Message D2Messages::wakeUpCanRequest{
    common::D2Message::makeD2RawMessage(0xFF, {0xC8})};
const D2Message D2Messages::goToSleepCanRequest{
    common::D2Message::makeD2RawMessage(0xFF, {0x86})};
const D2Message D2Messages::startTCMAdaptMsg{
    common::D2Message(static_cast<uint8_t>(common::ECUType::TCM), {0xB2, 0x50})};
const D2Message D2Messages::enableCommunicationMsg{
    common::D2Message(static_cast<uint8_t>(common::ECUType::CEM), {0xD8})};

} // namespace common
