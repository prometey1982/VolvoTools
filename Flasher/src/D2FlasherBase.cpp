#include "flasher/D2FlasherBase.hpp"

#include <common/Util.hpp>
#include <common/D2Message.hpp>
#include <common/SBL.hpp>
#include <common/VBFParser.hpp>
#include <j2534/J2534.hpp>
#include <j2534/J2534Channel.hpp>

#include <algorithm>
#include <numeric>
#include <time.h>

namespace {

bool writeMessagesAndCheckAnswer(j2534::J2534Channel &channel,
                                 const std::vector<PASSTHRU_MSG> &msgs,
                                 uint8_t toCheck, size_t count = 10) {
  channel.clearRx();
  unsigned long msgsNum = msgs.size();
  const auto error = channel.writeMsgs(msgs, msgsNum, 5000);
  if (error != STATUS_NOERROR) {
    throw std::runtime_error("write msgs error");
  }
  std::vector<PASSTHRU_MSG> received_msgs(1);
  for (size_t i = 0; i < count; ++i) {
    channel.readMsgs(received_msgs, 3000);
    for (const auto &msg : received_msgs) {
      if (msg.Data[5] == toCheck) {
        return true;
      }
    }
  }
  return false;
}

bool writeMessagesAndCheckAnswer2(j2534::J2534Channel &channel,
                                 const std::vector<PASSTHRU_MSG> &msgs,
                                 uint8_t toCheck5, uint8_t toCheck6, size_t count = 10) {
  unsigned long msgsNum = msgs.size();
  const auto error = channel.writeMsgs(msgs, msgsNum, 5000);
  if (error != STATUS_NOERROR) {
    throw std::runtime_error("write msgs error");
  }
  for (int i = 0; i < count; ++i) {
    std::vector<PASSTHRU_MSG> received_msgs(1);
    channel.readMsgs(received_msgs, 3000);
    for (const auto &read : received_msgs) {
      if (read.Data[5] == toCheck5 && read.Data[6] == toCheck6) {
        return true;
      }
    }
  }
  return false;
}

uint8_t calculateCheckSum(const std::vector<uint8_t> &bin, size_t beginOffset,
                          size_t endOffset) {
  uint32_t sum = 0;
  for (size_t i = beginOffset; i < endOffset; ++i) {
    sum += bin[i];
  }
  do {
    sum = ((sum >> 24) & 0xFF) + ((sum >> 16) & 0xFF) + ((sum >> 8) & 0xFF) +
          (sum & 0xFF);
  } while (((sum >> 8) & 0xFFFFFF) != 0);
  return static_cast<uint8_t>(sum);
}
} // namespace

namespace flasher {
D2FlasherBase::D2FlasherBase(j2534::J2534 &j2534, unsigned long baudrate)
    : FlasherBase{ j2534 }
    , _baudrate{ baudrate }
{}

D2FlasherBase::~D2FlasherBase() { }

void D2FlasherBase::canWakeUp(unsigned long baudrate) {
  const unsigned long protocolId = CAN;
  const unsigned long flags = CAN_29BIT_ID;
  _channels.push_back(common::openChannel(getJ2534(), protocolId, flags, baudrate));
  _channels.push_back(common::openLowSpeedChannel(getJ2534(), flags));
  canWakeUp();
  cleanErrors();
}

unsigned long D2FlasherBase::getBaudrate() const {
  return _baudrate;
}

common::ECUType D2FlasherBase::getEcuType(common::CMType cmType) const {
  switch (cmType) {
  case common::CMType::ECM_ME7:
    return common::ECUType::ECM_ME;
  case common::CMType::ECM_ME9_P1:
    return common::ECUType::ECM_ME;
  case common::CMType::TCM_AW55_P2:
  case common::CMType::TCM_TF80_P2:
    return common::ECUType::TCM;
  default:
    throw std::runtime_error("Unsupported CMType");
  }
}

void D2FlasherBase::openChannels(unsigned long baudrate,
                             bool additionalConfiguration) {
  setCurrentState(FlasherState::OpenChannels);
  const unsigned long protocolId = CAN;
  const unsigned long flags = CAN_29BIT_ID;

  _channels.push_back(common::openChannel(getJ2534(), protocolId, flags, baudrate,
                                          additionalConfiguration));
  _channels.push_back(common::openLowSpeedChannel(getJ2534(), flags));
  if (baudrate != 500000) {
    _channels.push_back(common::openBridgeChannel(getJ2534()));
  }
}

void D2FlasherBase::resetChannels() {
  setCurrentState(FlasherState::CloseChannels);
  _channels.clear();
}

void D2FlasherBase::selectAndWriteBootloader(common::CMType cmType,
                                         unsigned long protocolId,
                                         unsigned long flags) {
  uint32_t bootloaderOffset = 0;
  common::ECUType ecuType = common::ECUType::ECM_ME;

  switch (cmType) {
  case common::CMType::ECM_ME7:
    ecuType = common::ECUType::ECM_ME;
    break;
  case common::CMType::ECM_ME9_P1:
    ecuType = common::ECUType::ECM_ME;
    break;
  case common::CMType::TCM_AW55_P2:
  case common::CMType::TCM_TF80_P2:
    ecuType = common::ECUType::TCM;
    break;
  }

  const auto bootloader = getSBL(cmType);
  if (bootloader.chunks.empty())
    throw std::runtime_error("Secondary bootloader not found");

  unsigned long msgsNum = 1;
  _channels[0]->writeMsgs(
      common::D2Messages::createWakeUpECUMsg(ecuType).toPassThruMsgs(protocolId,
                                                                     flags),
      msgsNum, 5000);

  canGoToSleep(protocolId, flags);
  std::this_thread::sleep_for(std::chrono::seconds(1));
  writeStartPrimaryBootloaderMsgAndCheckAnswer(ecuType, protocolId, flags);
  std::this_thread::sleep_for(std::chrono::seconds(1));
  writeSBL(ecuType, bootloader, protocolId, flags);
  std::this_thread::sleep_for(std::chrono::seconds(1));
}

common::VBF D2FlasherBase::getSBL(common::CMType cmType) const {
  common::VBFParser parser;
  switch (cmType) {
  case common::CMType::ECM_ME7:
    return parser.parse(common::SBLData::P2_ME7_SBL);
  case common::CMType::ECM_ME9_P1:
    return parser.parse(common::SBLData::P1_ME9_SBL);
  default:
      return common::VBF({}, {});
  }
}

void D2FlasherBase::canGoToSleep(unsigned long protocolId, unsigned long flags) {
  setCurrentState(FlasherState::FallAsleep);
  unsigned long channel1MsgId;
  unsigned long channel2MsgId;
  _channels[0]->startPeriodicMsg(
      common::D2Messages::goToSleepCanRequest.toPassThruMsgs(protocolId,
                                                             flags)[0],
      channel1MsgId, 5);
  if (_channels[1]) {
    _channels[1]->startPeriodicMsg(
        common::D2Messages::goToSleepCanRequest.toPassThruMsgs(
            CAN_PS /*protocolId*/, CAN_29BIT_ID)[0],
        channel2MsgId, 5);
  }
  std::this_thread::sleep_for(std::chrono::seconds(3));
  if (_channels[1]) {
    _channels[1]->stopPeriodicMsg(channel2MsgId);
  }
  _channels[0]->stopPeriodicMsg(channel1MsgId);
}

void D2FlasherBase::canWakeUp() {
  setCurrentState(FlasherState::WakeUp);
  unsigned long numMsgs = 1;
  _channels[0]->writeMsgs(common::D2Messages::wakeUpCanRequest, numMsgs, 5000);
  if (_channels[1]) {
    _channels[1]->writeMsgs(common::D2Messages::wakeUpCanRequest, numMsgs, 5000);

    std::this_thread::sleep_for(std::chrono::seconds(2));

    const auto now{std::chrono::system_clock::now()};
    const auto time_t = std::chrono::system_clock::to_time_t(now);
    struct tm lt;
    localtime_s(&lt, &time_t);

    _channels[1]->writeMsgs(
        common::D2Messages::setCurrentTime(lt.tm_hour, lt.tm_min), numMsgs,
        5000);
  }
}

void D2FlasherBase::cleanErrors() {
  for (const auto ecuType :
       {common::ECUType::ECM_ME, common::ECUType::TCM, common::ECUType::SRS}) {
    unsigned long numMsgs = 1;
    _channels[0]->writeMsgs(common::D2Messages::clearDTCMsgs(ecuType), numMsgs);
    _channels[1]->writeMsgs(common::D2Messages::clearDTCMsgs(ecuType), numMsgs);
  }
}
void D2FlasherBase::writeStartPrimaryBootloaderMsgAndCheckAnswer(
    common::ECUType ecuType, unsigned long protocolId, unsigned long flags) {
  if (!writeMessagesAndCheckAnswer(
          *_channels[0],
          common::D2Messages::createStartPrimaryBootloaderMsg(ecuType)
              .toPassThruMsgs(protocolId, flags),
          0xC6))
    throw std::runtime_error("CM didn't response with correct answer");
}

void D2FlasherBase::writeDataOffsetAndCheckAnswer(common::ECUType ecuType,
                                              uint32_t writeOffset,
                                              unsigned long protocolId,
                                              unsigned long flags) {
  const auto writeOffsetMsgs{
      common::D2Messages::createSetMemoryAddrMsg(ecuType, writeOffset)
          .toPassThruMsgs(protocolId, flags)};
  for (int i = 0; i < 10; ++i) {
    if (writeMessagesAndCheckAnswer(*_channels[0], writeOffsetMsgs, 0x9C))
      return;
    std::this_thread::sleep_for(std::chrono::seconds(1));
  }
  throw std::runtime_error("CM didn't response with correct answer");
}

void D2FlasherBase::writeSBL(common::ECUType ecuType, const common::VBF &bootloader,
                         unsigned long protocolId, unsigned long flags) {
  setCurrentState(FlasherState::LoadBootloader);
  for (size_t i = 0; i < bootloader.chunks.size(); ++i) {
    const auto &chunk = bootloader.chunks[i];
    auto bootloaderMsgs =
        common::D2Messages::createWriteDataMsgs(ecuType, chunk.data);
    writeDataOffsetAndCheckAnswer(ecuType, chunk.writeOffset, protocolId,
                                  flags);
    for (auto message : bootloaderMsgs) {
      const auto passThruMsgs = message.toPassThruMsgs(protocolId, flags);
      unsigned long numMsgs = passThruMsgs.size();
      _channels[0]->writeMsgs(passThruMsgs, numMsgs, 240000);
      if (numMsgs != passThruMsgs.size())
        throw std::runtime_error("Bootloader writing failed");
    }
    unsigned long numMsgs = 1;
    _channels[0]->writeMsgs(
        common::D2Messages::createSBLTransferCompleteMsg(ecuType)
            .toPassThruMsgs(protocolId, flags),
        numMsgs);
    writeDataOffsetAndCheckAnswer(ecuType, chunk.writeOffset, protocolId,
                                  flags);
    if (!writeMessagesAndCheckAnswer(
            *_channels[0],
            common::D2Messages::createCalculateChecksumMsg(
                ecuType, chunk.writeOffset + chunk.data.size())
                .toPassThruMsgs(protocolId, flags),
            0xB1))
      throw std::runtime_error("Can't read memory after bootloader");
  }
  setCurrentState(FlasherState::StartBootloader);
  writeDataOffsetAndCheckAnswer(ecuType, bootloader.header.call, protocolId,
                                flags);
  if (!writeMessagesAndCheckAnswer(
          *_channels[0],
          common::D2Messages::createJumpToMsg(ecuType).toPassThruMsgs(
              protocolId, flags),
          0xA0))
    throw std::runtime_error("Can't start bootloader");
}

void D2FlasherBase::writeChunk(common::ECUType ecuType,
                           const std::vector<uint8_t> &bin,
                           uint32_t beginOffset, uint32_t endOffset,
                           unsigned long protocolId, unsigned long flags) {
  setCurrentState(FlasherState::WriteFlash);
  auto storedProgress = getCurrentProgress();
  auto binMsgs = common::D2Messages::createWriteDataMsgs(
      ecuType, bin, beginOffset, endOffset);
  writeDataOffsetAndCheckAnswer(ecuType, beginOffset, protocolId, flags);
  for (const auto binMsg : binMsgs) {
    _channels[0]->clearRx();
    const auto passThruMsgs = binMsg.toPassThruMsgs(protocolId, flags);
    unsigned long msgsNum = passThruMsgs.size();
    const auto error = _channels[0]->writeMsgs(passThruMsgs, msgsNum, 50000);
    if (error != STATUS_NOERROR) {
      throw std::runtime_error("write msgs error");
    }
    storedProgress += 6 * passThruMsgs.size();
    setCurrentProgress(storedProgress);
  }
  setCurrentProgress(endOffset);
  writeDataOffsetAndCheckAnswer(ecuType, beginOffset, protocolId, flags);
  uint8_t checksum = calculateCheckSum(bin, beginOffset, endOffset);
  if (!writeMessagesAndCheckAnswer2(
          *_channels[0],
          common::D2Messages::createCalculateChecksumMsg(ecuType, endOffset)
              .toPassThruMsgs(protocolId, flags),
          0xB1, checksum))
    throw std::runtime_error("Failed. Checksums are not equal.");
}

void D2FlasherBase::writeChunk(common::ECUType ecuType,
                               const std::vector<uint8_t> &bin,
                               uint32_t writeOffset,
                               unsigned long protocolId, unsigned long flags) {
  setCurrentState(FlasherState::WriteFlash);
  auto storedProgress = getCurrentProgress();
  auto binMsgs = common::D2Messages::createWriteDataMsgs(
      ecuType, bin, 0, bin.size());
  writeDataOffsetAndCheckAnswer(ecuType, writeOffset, protocolId, flags);
  for (const auto binMsg : binMsgs) {
    _channels[0]->clearRx();
    const auto passThruMsgs = binMsg.toPassThruMsgs(protocolId, flags);
    unsigned long msgsNum = passThruMsgs.size();
    const auto error = _channels[0]->writeMsgs(passThruMsgs, msgsNum, 50000);
    if (error != STATUS_NOERROR) {
      throw std::runtime_error("write msgs error");
    }
    storedProgress += 6 * passThruMsgs.size();
    setCurrentProgress(storedProgress);
  }
  const uint32_t endOffset = writeOffset + bin.size();
  setCurrentProgress(endOffset);
  writeDataOffsetAndCheckAnswer(ecuType, writeOffset, protocolId, flags);
  uint8_t checksum = calculateCheckSum(bin, writeOffset, endOffset);
  if (!writeMessagesAndCheckAnswer2(
          *_channels[0],
          common::D2Messages::createCalculateChecksumMsg(ecuType, endOffset)
              .toPassThruMsgs(protocolId, flags),
          0xB1, checksum))
    throw std::runtime_error("Failed. Checksums are not equal.");
}

void D2FlasherBase::eraseMemory(common::ECUType ecuType, uint32_t offset,
                            unsigned long protocolId, unsigned long flags,
                            uint8_t toCheck) {
  writeDataOffsetAndCheckAnswer(ecuType, offset, protocolId, flags);
  if (!writeMessagesAndCheckAnswer(
          *_channels[0],
          common::D2Messages::createEraseMsg(ecuType).toPassThruMsgs(protocolId,
                                                                     flags),
          toCheck, 30))
    throw std::runtime_error("Can't erase memory");
}

void D2FlasherBase::eraseMemory2(common::ECUType ecuType, uint32_t offset,
    unsigned long protocolId, unsigned long flags,
    uint8_t toCheck, uint8_t toCheck2) {
    writeDataOffsetAndCheckAnswer(ecuType, offset, protocolId, flags);
    if (!writeMessagesAndCheckAnswer2(
        *_channels[0],
        common::D2Messages::createEraseMsg(ecuType).toPassThruMsgs(protocolId,
            flags),
        toCheck, toCheck2, 30))
        throw std::runtime_error("Can't erase memory");
}

void D2FlasherBase::writeFlashMe7(const std::vector<uint8_t> &bin,
                              unsigned long protocolId, unsigned long flags) {
  const auto ecuType = common::ECUType::ECM_ME;
  eraseMemory(ecuType, 0x8000, protocolId, flags, 0xF9);
  std::this_thread::sleep_for(std::chrono::seconds(3));
  eraseMemory(ecuType, 0x10000, protocolId, flags, 0xF9);
  writeChunk(ecuType, bin, 0x8000, 0xE000, protocolId, flags);
  writeChunk(ecuType, bin, 0x10000, bin.size(), protocolId, flags);
}

void D2FlasherBase::writeFlashMe9(const std::vector<uint8_t> &bin,
                              unsigned long protocolId, unsigned long flags) {
  const auto ecuType = common::ECUType::ECM_ME;
  eraseMemory2(ecuType, 0x20000, protocolId, flags, 0xF9, 0x0);
  writeChunk(ecuType, bin, 0x20000, 0x90000, protocolId, flags);
  writeChunk(ecuType, bin, 0xA0000, 0x1F0000, protocolId, flags);
}

void D2FlasherBase::writeFlashTCM(const std::vector<uint8_t> &bin,
                              unsigned long protocolId, unsigned long flags) {
  const auto ecuType = common::ECUType::TCM;
  const std::vector<uint32_t> chunks{0x8000,  0x10000, 0x20000,
                                     0x30000, 0x40000, 0x50000,
                                     0x60000, 0x70000, 0x80000};
  for (size_t i = 0; i < chunks.size() - 1; ++i) {
    eraseMemory(ecuType, chunks[i], protocolId, flags, 0xF9);
    writeChunk(ecuType, bin, chunks[i], chunks[i + 1], protocolId, flags);
    setCurrentProgress(chunks[i + 1]);
  }
}

} // namespace flasher
