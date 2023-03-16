#include "D2Flasher.hpp"

#include "../Common/Util.hpp"
#include "../common/D2Message.hpp"
#include "../j2534/J2534.hpp"
#include "../j2534/J2534Channel.hpp"

#include <algorithm>
#include <time.h>

namespace {
bool writeMessagesAndCheckAnswer(j2534::J2534Channel &channel,
                                 const std::vector<PASSTHRU_MSG> &msgs,
                                 uint8_t toCheck) {
  channel.clearRx();
  unsigned long msgsNum = msgs.size();
  const auto error = channel.writeMsgs(msgs, msgsNum, 5000);
  if (error != STATUS_NOERROR) {
    throw std::runtime_error("write msgs error");
  }
  std::vector<PASSTHRU_MSG> received_msgs(1);
  for (size_t i = 0; i < 50; ++i) {
    channel.readMsgs(received_msgs, 10000);
    for (const auto &msg : received_msgs) {
      if (msg.Data[5] == toCheck) {
        return true;
      }
    }
  }
  return false;
}
bool writeMessagesAndCheckAnswer(j2534::J2534Channel &channel,
                                 const std::vector<PASSTHRU_MSG> &msgs,
                                 uint8_t toCheck5, uint8_t toCheck6) {
  unsigned long msgsNum = msgs.size();
  const auto error = channel.writeMsgs(msgs, msgsNum, 5000);
  if (error != STATUS_NOERROR) {
    throw std::runtime_error("write msgs error");
  }
  for (int i = 0; i < 50; ++i) {
    std::vector<PASSTHRU_MSG> received_msgs(1);
    channel.readMsgs(received_msgs, 10000);
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
D2Flasher::D2Flasher(j2534::J2534 &j2534)
    : _j2534{j2534}, _currentState{State::Initial}, _currentProgress{0},
      _maximumProgress{0} {}

D2Flasher::~D2Flasher() { stop(); }

void D2Flasher::canWakeUp(unsigned long baudrate) {
  const unsigned long protocolId = CAN_XON_XOFF;
  const unsigned long flags = CAN_29BIT_ID;
  _channel1 = common::openChannel(_j2534, protocolId, flags, baudrate);
  _channel2 = common::openChannel(_j2534, protocolId, CAN_29BIT_ID, 125000);
  canWakeUp(protocolId, flags);
  cleanErrors(protocolId, flags);
}

void D2Flasher::registerCallback(FlasherCallback &callback) {
  std::unique_lock<std::mutex> lock{_mutex};
  _callbacks.push_back(&callback);
}

void D2Flasher::unregisterCallback(FlasherCallback &callback) {
  std::unique_lock<std::mutex> lock{_mutex};
  _callbacks.erase(std::remove(_callbacks.begin(), _callbacks.end(), &callback),
                   _callbacks.end());
}

void D2Flasher::flash(CMType cmType, unsigned long baudrate,
                      const std::vector<uint8_t> &bin) {
  messageToCallbacks("Initializing");
  const unsigned long protocolId = CAN;
  //  const unsigned long protocolId = CAN_XON_XOFF;
  const unsigned long flags = CAN_29BIT_ID;

  openChannels(baudrate, true);

  setMaximumProgress(bin.size());
  setCurrentProgress(0);

  setState(State::InProgress);

  _flasherThread = std::thread([this, cmType, bin, protocolId, flags] {
    flasherFunction(cmType, bin, protocolId, flags);
  });
}

void D2Flasher::stop() {
  messageToCallbacks("Stopping flasher");
  if (_flasherThread.joinable()) {
    _flasherThread.join();
  }
  resetChannels();
  messageToCallbacks("Flasher stopped");
}

D2Flasher::State D2Flasher::getState() const {
  std::unique_lock<std::mutex> lock{_mutex};
  return _currentState;
}

size_t D2Flasher::getCurrentProgress() const {
  std::unique_lock<std::mutex> lock(_mutex);
  return _currentProgress;
}

size_t D2Flasher::getMaximumProgress() const {
  std::unique_lock<std::mutex> lock(_mutex);
  return _maximumProgress;
}

void D2Flasher::openChannels(unsigned long baudrate,
                             bool additionalConfiguration) {
  // const unsigned long protocolId = CAN_XON_XOFF;
  //  const unsigned long protocolId = CAN_PS;
  const unsigned long protocolId = CAN;
  const unsigned long flags = CAN_29BIT_ID;

  _channel1 = common::openChannel(_j2534, protocolId, flags, baudrate,
                                  additionalConfiguration);
  _channel2 = common::openLowSpeedChannel(_j2534, flags);
  if (baudrate != 500000)
    _channel3 = common::openBridgeChannel(_j2534);
}

void D2Flasher::resetChannels() {
  _channel1.reset();
  _channel2.reset();
  _channel3.reset();
}

void D2Flasher::selectAndWriteBootloader(CMType cmType,
                                         unsigned long protocolId,
                                         unsigned long flags) {
  uint32_t bootloaderOffset = 0;
  common::ECUType ecuType = common::ECUType::ECM_ME;

  switch (cmType) {
  case CMType::ECM_ME7:
    bootloaderOffset = 0x31C000;
    ecuType = common::ECUType::ECM_ME;
    break;
  case CMType::ECM_ME9:
    bootloaderOffset = 0x7F81D0;
    ecuType = common::ECUType::ECM_ME;
    break;
  case CMType::TCM_AW55:
  case CMType::TCM_TF80:
    bootloaderOffset = 0xFFFF8200;
    ecuType = common::ECUType::TCM;
    break;
  }

  const auto bootloader = getSBL(cmType);
  if (bootloader.chunks.empty())
    throw std::runtime_error("Secondary bootloader not found");

  unsigned long msgsNum = 1;
  _channel1->writeMsgs(
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

VBF D2Flasher::getSBL(CMType cmType) const {
  switch (cmType) {
  case CMType::ECM_ME7:
    return VBF(0x31C000,
               {VBFChunk(0x31C000, common::D2Messages::me7BootLoader)});
  case CMType::ECM_ME9:
    return VBF(0x7F81D0,
               {VBFChunk(0x7F81D0, common::D2Messages::me9BootLoader)});
  default:
    return VBF(0, {});
  }
}

void D2Flasher::canGoToSleep(unsigned long protocolId, unsigned long flags) {
  unsigned long channel1MsgId;
  unsigned long channel2MsgId;
  _channel1->startPeriodicMsg(
      common::D2Messages::goToSleepCanRequest.toPassThruMsgs(protocolId,
                                                             flags)[0],
      channel1MsgId, 5);
  if (_channel2) {
    _channel2->startPeriodicMsg(
        common::D2Messages::goToSleepCanRequest.toPassThruMsgs(
            CAN_PS /*protocolId*/, CAN_29BIT_ID)[0],
        channel2MsgId, 5);
  }
  std::this_thread::sleep_for(std::chrono::seconds(3));
  if (_channel2) {
    _channel2->stopPeriodicMsg(channel2MsgId);
  }
  _channel1->stopPeriodicMsg(channel1MsgId);
}

void D2Flasher::canWakeUp(unsigned long protocolId, unsigned long flags) {
  unsigned long numMsgs = 1;
  _channel1->writeMsgs(
      common::D2Messages::wakeUpCanRequest.toPassThruMsgs(protocolId, flags),
      numMsgs, 5000);
  if (_channel2) {
    _channel2->writeMsgs(common::D2Messages::wakeUpCanRequest.toPassThruMsgs(
                             CAN_PS, CAN_29BIT_ID),
                         numMsgs, 5000);

    std::this_thread::sleep_for(std::chrono::seconds(2));

    const auto now{std::chrono::system_clock::now()};
    const auto time_t = std::chrono::system_clock::to_time_t(now);
    struct tm lt;
    localtime_s(&lt, &time_t);

    _channel2->writeMsgs(
        common::D2Messages::setCurrentTime(lt.tm_hour, lt.tm_min)
            .toPassThruMsgs(CAN_PS, CAN_29BIT_ID),
        numMsgs, 5000);
  }
}

void D2Flasher::cleanErrors(unsigned long protocolId, unsigned long flags) {
  for (const auto ecuType :
       {common::ECUType::ECM_ME, common::ECUType::TCM, common::ECUType::SRS}) {
    unsigned long numMsgs = 1;
    _channel1->writeMsgs(
        common::D2Messages::clearDTCMsgs(ecuType).toPassThruMsgs(protocolId,
                                                                 flags),
        numMsgs);
    _channel2->writeMsgs(
        common::D2Messages::clearDTCMsgs(ecuType).toPassThruMsgs(CAN_PS,
                                                                 CAN_29BIT_ID),
        numMsgs);
  }
}
void D2Flasher::writeStartPrimaryBootloaderMsgAndCheckAnswer(
    common::ECUType ecuType, unsigned long protocolId, unsigned long flags) {
  if (!writeMessagesAndCheckAnswer(
          *_channel1,
          common::D2Messages::createStartPrimaryBootloaderMsg(ecuType)
              .toPassThruMsgs(protocolId, flags),
          0xC6))
    throw std::runtime_error("CM didn't response with correct answer");
}

void D2Flasher::writeDataOffsetAndCheckAnswer(common::ECUType ecuType,
                                              uint32_t writeOffset,
                                              unsigned long protocolId,
                                              unsigned long flags) {
  const auto writeOffsetMsgs{
      common::D2Messages::createSetMemoryAddrMsg(ecuType, writeOffset)
          .toPassThruMsgs(protocolId, flags)};
  for (int i = 0; i < 10; ++i) {
    if (writeMessagesAndCheckAnswer(*_channel1, writeOffsetMsgs, 0x9C))
      return;
    std::this_thread::sleep_for(std::chrono::seconds(1));
  }
  throw std::runtime_error("CM didn't response with correct answer");
}

void D2Flasher::writeSBL(common::ECUType ecuType, const VBF &bootloader,
                         unsigned long protocolId, unsigned long flags) {
  messageToCallbacks("Writing bootloader");
  for (size_t i = 0; i < bootloader.chunks.size(); ++i) {
    const auto &chunk = bootloader.chunks[i];
    auto bootloaderMsgs =
        common::D2Messages::createWriteDataMsgs(ecuType, chunk.data);
    writeDataOffsetAndCheckAnswer(ecuType, chunk.writeOffset, protocolId,
                                  flags);
    for (auto message : bootloaderMsgs) {
      const auto passThruMsgs = message.toPassThruMsgs(protocolId, flags);
      unsigned long numMsgs = passThruMsgs.size();
      _channel1->writeMsgs(passThruMsgs, numMsgs, 240000);
      if (numMsgs != passThruMsgs.size())
        throw std::runtime_error("Bootloader writing failed");
    }
    unsigned long numMsgs = 1;
    _channel1->writeMsgs(
        common::D2Messages::createSBLTransferCompleteMsg(ecuType)
            .toPassThruMsgs(protocolId, flags),
        numMsgs);
    writeDataOffsetAndCheckAnswer(ecuType, chunk.writeOffset, protocolId,
                                  flags);
    if (!writeMessagesAndCheckAnswer(
            *_channel1,
            common::D2Messages::createCalculateChecksumMsg(
                ecuType, chunk.writeOffset + chunk.data.size())
                .toPassThruMsgs(protocolId, flags),
            0xB1))
      throw std::runtime_error("Can't read memory after bootloader");
  }
  writeDataOffsetAndCheckAnswer(ecuType, bootloader.jumpAddr, protocolId,
                                flags);
  if (!writeMessagesAndCheckAnswer(
          *_channel1,
          common::D2Messages::createJumpToMsg(ecuType).toPassThruMsgs(
              protocolId, flags),
          0xA0))
    throw std::runtime_error("Can't start bootloader");
}

void D2Flasher::writeChunk(common::ECUType ecuType,
                           const std::vector<uint8_t> &bin,
                           uint32_t beginOffset, uint32_t endOffset,
                           unsigned long protocolId, unsigned long flags) {
  messageToCallbacks("Writing chunk");
  auto storedProgress = getCurrentProgress();
  auto binMsgs = common::D2Messages::createWriteDataMsgs(
      ecuType, bin, beginOffset, endOffset);
  writeDataOffsetAndCheckAnswer(ecuType, beginOffset, protocolId, flags);
  for (const auto binMsg : binMsgs) {
    _channel1->clearRx();
    const auto passThruMsgs = binMsg.toPassThruMsgs(protocolId, flags);
    unsigned long msgsNum = passThruMsgs.size();
    const auto error = _channel1->writeMsgs(passThruMsgs, msgsNum, 50000);
    if (error != STATUS_NOERROR) {
      throw std::runtime_error("write msgs error");
    }
    storedProgress += 6 * passThruMsgs.size();
    setCurrentProgress(storedProgress);
  }
  setCurrentProgress(endOffset);
  writeDataOffsetAndCheckAnswer(ecuType, beginOffset, protocolId, flags);
  uint8_t checksum = calculateCheckSum(bin, beginOffset, endOffset);
  if (!writeMessagesAndCheckAnswer(
          *_channel1,
          common::D2Messages::createCalculateChecksumMsg(ecuType, endOffset)
              .toPassThruMsgs(protocolId, flags),
          0xB1, checksum))
    throw std::runtime_error("Failed. Checksums are not equal.");
}

void D2Flasher::eraseMemory(common::ECUType ecuType, uint32_t offset,
                            unsigned long protocolId, unsigned long flags,
                            uint8_t toCheck) {
  writeDataOffsetAndCheckAnswer(ecuType, offset, protocolId, flags);
  if (!writeMessagesAndCheckAnswer(
          *_channel1,
          common::D2Messages::createEraseMsg(ecuType).toPassThruMsgs(protocolId,
                                                                     flags),
          toCheck))
    throw std::runtime_error("Can't erase memory");
}

void D2Flasher::writeFlashMe7(const std::vector<uint8_t> &bin,
                              unsigned long protocolId, unsigned long flags) {
  const auto ecuType = common::ECUType::ECM_ME;
  eraseMemory(ecuType, 0x8000, protocolId, flags, 0xF9);
  std::this_thread::sleep_for(std::chrono::seconds(3));
  eraseMemory(ecuType, 0x10000, protocolId, flags, 0xF9);
  writeChunk(ecuType, bin, 0x8000, 0xE000, protocolId, flags);
  writeChunk(ecuType, bin, 0x10000, bin.size(), protocolId, flags);
}

void D2Flasher::writeFlashMe9(const std::vector<uint8_t> &bin,
                              unsigned long protocolId, unsigned long flags) {
  const auto ecuType = common::ECUType::ECM_ME;
  eraseMemory(ecuType, 0x20000, protocolId, flags, 0x0);
  writeChunk(ecuType, bin, 0x20000, 0x90000, protocolId, flags);
  writeChunk(ecuType, bin, 0xA0000, 0x1F0000, protocolId, flags);
}

void D2Flasher::writeFlashTCM(const std::vector<uint8_t> &bin,
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

void D2Flasher::flasherFunction(CMType cmType, const std::vector<uint8_t> bin,
                                unsigned long protocolId, unsigned long flags) {
  try {
    selectAndWriteBootloader(cmType, protocolId, flags);
    switch (cmType) {
    case CMType::ECM_ME7:
      writeFlashMe7(bin, protocolId, flags);
      break;
    case CMType::ECM_ME9:
      writeFlashMe9(bin, protocolId, flags);
      break;
    case CMType::TCM_AW55:
    case CMType::TCM_TF80:
      writeFlashTCM(bin, protocolId, flags);
      break;
    }
    canWakeUp(protocolId, flags);
    setState(State::Done);
  } catch (std::exception &ex) {
    canWakeUp(protocolId, flags);
    messageToCallbacks(ex.what());
    setState(State::Error);
  }
}

void D2Flasher::setState(State newState) {
  std::unique_lock<std::mutex> lock{_mutex};
  _currentState = newState;
}

void D2Flasher::messageToCallbacks(const std::string &message) {
  for (const auto &callback : _callbacks) {
    callback->OnMessage(message);
  }
}

void D2Flasher::setCurrentProgress(size_t currentProgress) {
  std::unique_lock<std::mutex> lock(_mutex);
  _currentProgress = currentProgress;
}

void D2Flasher::setMaximumProgress(size_t maximumProgress) {
  std::unique_lock<std::mutex> lock(_mutex);
  _maximumProgress = maximumProgress;
}

} // namespace flasher
