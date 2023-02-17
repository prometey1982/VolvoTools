#include "Flasher.hpp"

#include "../Common/Util.hpp"
#include "../common/CEMCanMessage.hpp"
#include "../j2534/J2534.hpp"
#include "../j2534/J2534Channel.hpp"

#include <algorithm>
#include <time.h>

namespace {
bool writeMessagesAndCheckAnswer(j2534::J2534Channel &channel, const std::vector<PASSTHRU_MSG>& msgs,
                                uint8_t toCheck) {
  channel.clearRx();
  unsigned long msgsNum = msgs.size();
  const auto error = channel.writeMsgs(msgs, msgsNum, 5000);
  if (error != STATUS_NOERROR) {
    throw std::runtime_error("write msgs error");
  }
  std::vector<PASSTHRU_MSG> received_msgs(1);
  for (size_t i = 0; i < 50; ++i) {
    channel.readMsgs(received_msgs, 1000);
    for (const auto &msg : received_msgs) {
      if (msg.Data[5] == toCheck) {
        return true;
      }
    }
  }
  return false;
}
bool writeMessagesAndCheckAnswer(j2534::J2534Channel &channel, const std::vector<PASSTHRU_MSG>& msgs,
                                uint8_t toCheck5, uint8_t toCheck6) {
  unsigned long msgsNum = msgs.size();
  const auto error = channel.writeMsgs(msgs, msgsNum, 5000);
  if (error != STATUS_NOERROR) {
    throw std::runtime_error("write msgs error");
  }
  for (int i = 0; i < 50; ++i) {
    std::vector<PASSTHRU_MSG> received_msgs(1);
    channel.readMsgs(received_msgs, 1000);
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
Flasher::Flasher(j2534::J2534 &j2534)
    : _j2534{j2534}, _currentState{State::Initial}, _currentProgress{0},
      _maximumProgress{0} {}

Flasher::~Flasher() { stop(); }

void Flasher::canWakeUp(unsigned long baudrate) {
  const unsigned long protocolId = CAN_XON_XOFF;
  const unsigned long flags = CAN_29BIT_ID;
  _channel1 = common::openChannel(_j2534, protocolId, flags, baudrate);
  _channel2 =
      common::openChannel(_j2534, protocolId, CAN_29BIT_CHANNEL2, 125000);
  canWakeUp(protocolId, flags);
  cleanErrors(protocolId, flags);
}

void Flasher::registerCallback(FlasherCallback &callback) {
  std::unique_lock<std::mutex> lock{_mutex};
  _callbacks.push_back(&callback);
}

void Flasher::unregisterCallback(FlasherCallback &callback) {
  std::unique_lock<std::mutex> lock{_mutex};
  _callbacks.erase(std::remove(_callbacks.begin(), _callbacks.end(), &callback),
                   _callbacks.end());
}

void Flasher::flash(CMType cmType, unsigned long baudrate,
                    const std::vector<uint8_t> &bin) {
  messageToCallbacks("Initializing");
  const unsigned long protocolId = CAN_XON_XOFF;
  const unsigned long flags = CAN_29BIT_ID;

  openChannels(baudrate, true);

  setMaximumProgress(bin.size());
  setCurrentProgress(0);

  setState(State::InProgress);

  _flasherThread = std::thread([this, cmType, bin, protocolId, flags] {
    flasherFunction(cmType, bin, protocolId, flags);
  });
}

void Flasher::stop() {
  messageToCallbacks("Stopping flasher");
  if (_flasherThread.joinable()) {
    _flasherThread.join();
  }
  resetChannels();
  messageToCallbacks("Flasher stopped");
}

Flasher::State Flasher::getState() const {
  std::unique_lock<std::mutex> lock{_mutex};
  return _currentState;
}

size_t Flasher::getCurrentProgress() const {
  std::unique_lock<std::mutex> lock(_mutex);
  return _currentProgress;
}

size_t Flasher::getMaximumProgress() const {
  std::unique_lock<std::mutex> lock(_mutex);
  return _maximumProgress;
}

void Flasher::openChannels(unsigned long baudrate,
                           bool additionalConfiguration) {
//    const unsigned long protocolId = CAN_XON_XOFF;
    const unsigned long protocolId = CAN;
  const unsigned long flags = CAN_29BIT_ID;

  _channel1 = common::openChannel(_j2534, protocolId, flags, baudrate,
                                  additionalConfiguration);
  _channel2 =
          common::openChannel(_j2534, CAN_CH1, flags, 125000);
//  common::openChannel(_j2534, protocolId, CAN_29BIT_CHANNEL2, 125000);
  if (baudrate != 500000)
    _channel3 = common::openBridgeChannel(_j2534);
}

void Flasher::resetChannels() {
  _channel1.reset();
  _channel2.reset();
  _channel3.reset();
}

void Flasher::selectAndWriteBootloader(CMType cmType, unsigned long protocolId,
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

  unsigned long msgsNum = 1;
  _channel1->writeMsgs(
      common::CanMessages::createWakeUpECUMsg(ecuType).toPassThruMsgs(
          protocolId, flags),
      msgsNum, 5000);

  const auto bootloader = getSBL(cmType);
  if (bootloader.chunks.empty())
    throw std::runtime_error("Secondary bootloader not found");

  canGoToSleep(protocolId, flags);
  std::this_thread::sleep_for(std::chrono::seconds(1));
  writeStartPrimaryBootloaderMsgAndCheckAnswer(ecuType, protocolId, flags);
  std::this_thread::sleep_for(std::chrono::seconds(1));
  writeSBL(ecuType, bootloader, protocolId, flags);
  std::this_thread::sleep_for(std::chrono::seconds(1));
}

VBF Flasher::getSBL(CMType cmType) const {
  switch (cmType) {
  case CMType::ECM_ME7:
    return VBF(0x31C000,
               {VBFChunk(0x31C000, common::CanMessages::me7BootLoader)});
  case CMType::ECM_ME9:
    return VBF(0x7F81D0,
               {VBFChunk(0x7F81D0, common::CanMessages::me9BootLoader)});
  default:
    return VBF(0, {});
  }
}

void Flasher::canGoToSleep(unsigned long protocolId, unsigned long flags) {
  unsigned long channel1MsgId;
  unsigned long channel2MsgId;
  _channel1->startPeriodicMsg(
      common::CanMessages::goToSleepCanRequest.toPassThruMsgs(protocolId, flags)[0],
      channel1MsgId, 5);
  if (_channel2) {
    _channel2->startPeriodicMsg(
        common::CanMessages::goToSleepCanRequest.toPassThruMsgs(
            protocolId, CAN_29BIT_CHANNEL2)[0],
        channel2MsgId, 5);
  }
  std::this_thread::sleep_for(std::chrono::seconds(3));
  if (_channel2) {
    _channel2->stopPeriodicMsg(channel2MsgId);
  }
  _channel1->stopPeriodicMsg(channel1MsgId);
}

void Flasher::canWakeUp(unsigned long protocolId, unsigned long flags) {
  unsigned long numMsgs = 1;
  _channel1->writeMsgs(
      common::CanMessages::wakeUpCanRequest.toPassThruMsgs(protocolId, flags),
      numMsgs, 5000);
  if (_channel2) {
    _channel2->writeMsgs(common::CanMessages::wakeUpCanRequest.toPassThruMsgs(
                             protocolId, CAN_29BIT_CHANNEL2),
                         numMsgs, 5000);

    std::this_thread::sleep_for(std::chrono::seconds(2));

    const auto now{std::chrono::system_clock::now()};
    const auto time_t = std::chrono::system_clock::to_time_t(now);
    struct tm lt;
    localtime_s(&lt, &time_t);

    _channel2->writeMsgs(
        common::CanMessages::setCurrentTime(lt.tm_hour, lt.tm_min)
            .toPassThruMsgs(protocolId, CAN_29BIT_CHANNEL2),
        numMsgs, 5000);
  }
}

void Flasher::cleanErrors(unsigned long protocolId, unsigned long flags) {
  for (const auto ecuType :
       {common::ECUType::ECM_ME, common::ECUType::TCM, common::ECUType::SRS}) {
    unsigned long numMsgs = 1;
    _channel1->writeMsgs(
        common::CanMessages::clearDTCMsgs(ecuType).toPassThruMsgs(protocolId,
                                                                  flags),
        numMsgs);
    _channel2->writeMsgs(
        common::CanMessages::clearDTCMsgs(ecuType).toPassThruMsgs(
            protocolId, CAN_29BIT_CHANNEL2),
        numMsgs);
  }
}
void Flasher::writeStartPrimaryBootloaderMsgAndCheckAnswer(
    common::ECUType ecuType, unsigned long protocolId, unsigned long flags) {
  if (!writeMessagesAndCheckAnswer(
          *_channel1,
          common::CanMessages::createStartPrimaryBootloaderMsg(ecuType)
              .toPassThruMsgs(protocolId, flags),
          0xC6))
    throw std::runtime_error("CM didn't response with correct answer");
}

void Flasher::writeDataOffsetAndCheckAnswer(common::ECUType ecuType,
                                            uint32_t writeOffset,
                                            unsigned long protocolId,
                                            unsigned long flags) {
  const auto writeOffsetMsgs{
      common::CanMessages::createSetMemoryAddrMsg(ecuType, writeOffset)
          .toPassThruMsgs(protocolId, flags)};
  for (int i = 0; i < 10; ++i) {
    if (writeMessagesAndCheckAnswer(*_channel1, writeOffsetMsgs, 0x9C))
      return;
    std::this_thread::sleep_for(std::chrono::seconds(1));
  }
  throw std::runtime_error("CM didn't response with correct answer");
}

void Flasher::writeSBL(common::ECUType ecuType, const VBF &bootloader,
                       unsigned long protocolId, unsigned long flags) {
  messageToCallbacks("Writing bootloader");
  for (size_t i = 0; i < bootloader.chunks.size(); ++i) {
    const auto &chunk = bootloader.chunks[i];
    auto bootloaderMsgs =
        common::CanMessages::createWriteDataMsgs(ecuType, chunk.data)
            .toPassThruMsgs(protocolId, flags);
    unsigned long numMsgs = 1;
    writeDataOffsetAndCheckAnswer(ecuType, chunk.writeOffset, protocolId,
                                  flags);
    _channel1->writeMsgs(bootloaderMsgs, numMsgs, 240000);
    if (numMsgs != bootloaderMsgs.size())
      throw std::runtime_error("Bootloader writing failed");
    _channel1->writeMsgs(
        common::CanMessages::createSBLTransferCompleteMsg(ecuType)
            .toPassThruMsgs(protocolId, flags),
        numMsgs);
    writeDataOffsetAndCheckAnswer(ecuType, chunk.writeOffset, protocolId,
                                  flags);
    if (!writeMessagesAndCheckAnswer(
            *_channel1,
            common::CanMessages::createCalculateChecksumMsg(
                ecuType, chunk.writeOffset + chunk.data.size())
                .toPassThruMsgs(protocolId, flags),
            0xB1))
      throw std::runtime_error("Can't read memory after bootloader");
  }
  writeDataOffsetAndCheckAnswer(ecuType, bootloader.jumpAddr, protocolId,
                                flags);
  if (!writeMessagesAndCheckAnswer(
          *_channel1,
          common::CanMessages::createJumpToMsg(ecuType).toPassThruMsgs(
              protocolId, flags),
          0xA0))
    throw std::runtime_error("Can't start bootloader");
}

void Flasher::writeChunk(common::ECUType ecuType,
                         const std::vector<uint8_t> &bin, uint32_t beginOffset,
                         uint32_t endOffset, unsigned long protocolId,
                         unsigned long flags) {
  messageToCallbacks("Writing chunk");
  auto binMsgs = common::CanMessages::createWriteDataMsgs(
                     ecuType, bin, beginOffset, endOffset)
                     .toPassThruMsgs(protocolId, flags);
  writeDataOffsetAndCheckAnswer(ecuType, beginOffset, protocolId, flags);
  for (size_t i = 0; i < binMsgs.size(); ++i) {
    unsigned long msgsNum = 1;
    _channel1->clearRx();
    const auto error = _channel1->writeMsgs({binMsgs[i]}, msgsNum, 50000);
    if (error != STATUS_NOERROR) {
      throw std::runtime_error("write msgs error");
    }
  }
  setCurrentProgress(endOffset);
  writeDataOffsetAndCheckAnswer(ecuType, beginOffset, protocolId, flags);
  uint8_t checksum = calculateCheckSum(bin, beginOffset, endOffset);
  if (!writeMessagesAndCheckAnswer(
          *_channel1,
          common::CanMessages::createCalculateChecksumMsg(ecuType, endOffset)
              .toPassThruMsgs(protocolId, flags),
          0xB1, checksum))
    throw std::runtime_error("Failed. Checksums are not equal.");
}

void Flasher::eraseMemory(common::ECUType ecuType, uint32_t offset,
                          unsigned long protocolId, unsigned long flags,
                          uint8_t toCheck) {
  writeDataOffsetAndCheckAnswer(ecuType, offset, protocolId, flags);
  if (!writeMessagesAndCheckAnswer(
          *_channel1,
          common::CanMessages::createEraseMsg(ecuType).toPassThruMsgs(protocolId,
                                                                     flags),
          toCheck))
    throw std::runtime_error("Can't erase memory");
}

void Flasher::writeFlashMe7(const std::vector<uint8_t> &bin,
                            unsigned long protocolId, unsigned long flags) {
  const auto ecuType = common::ECUType::ECM_ME;
  eraseMemory(ecuType, 0x8000, protocolId, flags, 0xF9);
  std::this_thread::sleep_for(std::chrono::seconds(3));
  eraseMemory(ecuType, 0x10000, protocolId, flags, 0xF9);
  writeChunk(ecuType, bin, 0x8000, 0xE000, protocolId, flags);
  writeChunk(ecuType, bin, 0x10000, bin.size(), protocolId, flags);
}

void Flasher::writeFlashMe9(const std::vector<uint8_t> &bin,
                            unsigned long protocolId, unsigned long flags) {
  const auto ecuType = common::ECUType::ECM_ME;
  eraseMemory(ecuType, 0x20000, protocolId, flags, 0x0);
  writeChunk(ecuType, bin, 0x20000, 0x90000, protocolId, flags);
  writeChunk(ecuType, bin, 0xA0000, 0x1F0000, protocolId, flags);
}

void Flasher::writeFlashTCM(const std::vector<uint8_t> &bin,
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

void Flasher::flasherFunction(CMType cmType, const std::vector<uint8_t> bin,
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

void Flasher::setState(State newState) {
  std::unique_lock<std::mutex> lock{_mutex};
  _currentState = newState;
}

void Flasher::messageToCallbacks(const std::string &message) {
  for (const auto &callback : _callbacks) {
    callback->OnMessage(message);
  }
}

void Flasher::setCurrentProgress(size_t currentProgress) {
  std::unique_lock<std::mutex> lock(_mutex);
  _currentProgress = currentProgress;
}

void Flasher::setMaximumProgress(size_t maximumProgress) {
  std::unique_lock<std::mutex> lock(_mutex);
  _maximumProgress = maximumProgress;
}

} // namespace flasher
