#include "Flasher.hpp"

#include "../Common/Util.hpp"
#include "../common/CEMCanMessage.hpp"
#include "../common/CanMessages.hpp"
#include "../j2534/J2534.hpp"
#include "../j2534/J2534Channel.hpp"

#include <algorithm>
#include <time.h>

namespace {
bool writeMessageAndCheckAnswer(j2534::J2534Channel &channel, PASSTHRU_MSG msg,
                                uint8_t toCheck) {
  channel.clear();
  unsigned long msgsNum = 1;
  const auto error = channel.writeMsgs({msg}, msgsNum, 5000);
  if (error != STATUS_NOERROR) {
    throw std::runtime_error("write msgs error");
  }
  std::vector<PASSTHRU_MSG> msgs(1);
  for (size_t i = 0; i < 50; ++i) {
    channel.readMsgs(msgs, 10000);
    for (const auto &msg : msgs) {
      if (msg.Data[5] == toCheck) {
        return true;
      }
    }
    std::this_thread::sleep_for(std::chrono::seconds(1));
  }
  return false;
}
bool writeMessageAndCheckAnswer(j2534::J2534Channel &channel, PASSTHRU_MSG msg,
                                uint8_t toCheck5, uint8_t toCheck6) {
  unsigned long msgsNum = 1;
  const auto error = channel.writeMsgs({msg}, msgsNum, 5000);
  if (error != STATUS_NOERROR) {
    throw std::runtime_error("write msgs error");
  }
  std::vector<PASSTHRU_MSG> msgs(1);
  channel.readMsgs(msgs, 10000);
  for (const auto &read : msgs) {
    if (read.Data[5] == toCheck5 && read.Data[6] == toCheck6) {
      return true;
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
    : _j2534{j2534}, _currentState{State::Initial} {}

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

void Flasher::flash(unsigned long baudrate, const std::vector<uint8_t> &bin) {
  messageToCallbacks("Initializing");
  const unsigned long protocolId = CAN_XON_XOFF;
  const unsigned long flags = CAN_29BIT_ID;

  auto channel1 = common::openChannel(_j2534, protocolId, flags, baudrate);
  auto channel2 =
      common::openChannel(_j2534, protocolId, CAN_29BIT_CHANNEL2, 125000);
  std::unique_ptr<j2534::J2534Channel> channel3;
  if (baudrate != 500000)
    channel3 = common::openBridgeChannel(_j2534);
  flash(std::move(channel1), std::move(channel2), std::move(channel3), bin);
}

void Flasher::flash(std::unique_ptr<j2534::J2534Channel> &&channel1,
                    std::unique_ptr<j2534::J2534Channel> &&channel2,
                    std::unique_ptr<j2534::J2534Channel> &&channel3,
                    const std::vector<uint8_t> &bin) {
  _channel1 = std::move(channel1);
  _channel1 = std::move(channel2);
  _channel1 = std::move(channel3);

  setState(State::InProgress);

  const unsigned long protocolId = CAN_XON_XOFF;
  const unsigned long flags = CAN_29BIT_ID;
  _flasherThread = std::thread([this, bin, protocolId, flags] {
    flasherFunction(bin, protocolId, flags);
  });
}

void Flasher::stop() {
  messageToCallbacks("Stopping flasher");
  if (_flasherThread.joinable()) {
    _flasherThread.join();
  }
  _channel1.reset();
  _channel2.reset();
  _channel3.reset();
  messageToCallbacks("Flasher stopped");
}

Flasher::State Flasher::getState() const {
  std::unique_lock<std::mutex> lock{_mutex};
  return _currentState;
}

void Flasher::canGoToSleep(unsigned long protocolId, unsigned long flags) {
  unsigned long channel1MsgId;
  unsigned long channel2MsgId;
  _channel1->startPeriodicMsg(
      common::CanMessages::goToSleepCanRequest.toPassThruMsg(protocolId, flags),
      channel1MsgId, 5);
  if (_channel2) {
    _channel2->startPeriodicMsg(
        common::CanMessages::goToSleepCanRequest.toPassThruMsg(
            protocolId, CAN_29BIT_CHANNEL2),
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
void Flasher::writePreFlashMsgAndCheckAnswer(unsigned long protocolId,
                                             unsigned long flags) {
  if (!writeMessageAndCheckAnswer(
          *_channel1,
          common::CanMessages::preFlashECMMsg.toPassThruMsg(protocolId, flags),
          0xC6))
    throw std::runtime_error("ECM didn't response with correct answer");
}

void Flasher::writeDataOffsetAndCheckAnswer(uint32_t writeOffset,
                                            unsigned long protocolId,
                                            unsigned long flags) {
  const auto writeOffsetMsg{
      common::CanMessages::createWriteOffsetMsg(writeOffset)
          .toPassThruMsg(protocolId, flags)};
  for (int i = 0; i < 10; ++i) {
    if (writeMessageAndCheckAnswer(*_channel1, writeOffsetMsg, 0x9C))
      return;
    std::this_thread::sleep_for(std::chrono::seconds(1));
  }
  throw std::runtime_error("ECM didn't response with correct answer");
}

void Flasher::writeBootloader(uint32_t writeOffset,
                              const std::vector<uint8_t> &bootloader,
                              unsigned long protocolId, unsigned long flags) {
  messageToCallbacks("Writing bootloader");
  auto bootloaderMsgs = common::CanMessages::createWriteDataMsgs(bootloader)
                            .toPassThruMsgs(protocolId, flags);
  unsigned long numMsgs = 1;
  writeDataOffsetAndCheckAnswer(writeOffset, protocolId, flags);
  _channel1->writeMsgs(bootloaderMsgs, numMsgs, 240000);
  if (numMsgs != bootloaderMsgs.size())
    throw std::runtime_error("Bootloader writing failed");
  _channel1->writeMsgs(common::CanMessages::afterBootloaderFlash.toPassThruMsgs(
                           protocolId, flags),
                       numMsgs);
  writeDataOffsetAndCheckAnswer(writeOffset, protocolId, flags);
  if (!writeMessageAndCheckAnswer(*_channel1,
                                  common::CanMessages::createReadOffsetMsg(
                                      writeOffset + bootloader.size())
                                      .toPassThruMsg(protocolId, flags),
                                  0xB1))
    throw std::runtime_error("Can't read memory after bootloader");
  writeDataOffsetAndCheckAnswer(writeOffset, protocolId, flags);
  if (!writeMessageAndCheckAnswer(
          *_channel1,
          common::CanMessages::restartECMMsg.toPassThruMsg(protocolId, flags),
          0xA0))
    throw std::runtime_error("Can't start bootloader");
}

void Flasher::writeChunk(const std::vector<uint8_t> &bin, uint32_t beginOffset,
                         uint32_t endOffset, unsigned long protocolId,
                         unsigned long flags) {
  messageToCallbacks("Writing chunk");
  auto binMsgs =
      common::CanMessages::createWriteDataMsgs(bin, beginOffset, endOffset)
          .toPassThruMsgs(protocolId, flags);
  unsigned long numMsgs = 1;
  writeDataOffsetAndCheckAnswer(beginOffset, protocolId, flags);
  const auto error = _channel1->writeMsgs(binMsgs, numMsgs, 240000);
  if (error != STATUS_NOERROR) {
    throw std::runtime_error("write msgs error");
  }
  if (numMsgs != binMsgs.size())
    throw std::runtime_error("Binary writing failed");
  writeDataOffsetAndCheckAnswer(beginOffset, protocolId, flags);
  uint8_t checksum = calculateCheckSum(bin, beginOffset, endOffset);
  if (!writeMessageAndCheckAnswer(
          *_channel1,
          common::CanMessages::createReadOffsetMsg(endOffset).toPassThruMsg(
              protocolId, flags),
          0xB1, checksum))
    throw std::runtime_error("Failed. Checksums are not equal.");
}

void Flasher::testMemory(uint32_t offset, unsigned long protocolId,
                         unsigned long flags, uint8_t toCheck) {
  writeDataOffsetAndCheckAnswer(offset, protocolId, flags);
  if (!writeMessageAndCheckAnswer(
          *_channel1,
          common::CanMessages::testMemoryMsg.toPassThruMsg(protocolId, flags),
          toCheck))
    throw std::runtime_error("Can't test memory addr");
}

void Flasher::writeFlashMe7(const std::vector<uint8_t> &bin,
                            unsigned long protocolId, unsigned long flags) {
  testMemory(0x8000, protocolId, flags, 0xF9);
  std::this_thread::sleep_for(std::chrono::seconds(3));
  testMemory(0x10000, protocolId, flags, 0xF9);
  writeChunk(bin, 0x8000, 0xE000, protocolId, flags);
  writeChunk(bin, 0x10000, bin.size(), protocolId, flags);
}

void Flasher::writeFlashMe9(const std::vector<uint8_t> &bin,
                            unsigned long protocolId, unsigned long flags) {
  testMemory(0x20000, protocolId, flags, 0x0);
  writeChunk(bin, 0x20000, 0x90000, protocolId, flags);
  writeChunk(bin, 0xA0000, 0x1F0000, protocolId, flags);
}

void Flasher::flasherFunction(const std::vector<uint8_t> bin,
                              unsigned long protocolId, unsigned long flags) {
  try {
    unsigned long msgsNum = 1;
    _channel1->writeMsgs(
        common::CanMessages::wakeUpECM.toPassThruMsgs(protocolId, flags),
        msgsNum, 5000);
    canGoToSleep(protocolId, flags);
    writePreFlashMsgAndCheckAnswer(protocolId, flags);
    const bool isMe9 = (bin.size() == 0x200000);
    uint32_t bootloaderOffset = (isMe9 ? 0x7F81D0 : 0x31C000);
    const auto bootloader = (isMe9 ? common::CanMessages::me9BootLoader
                                   : common::CanMessages::me7BootLoader);
    writeBootloader(bootloaderOffset, bootloader, protocolId, flags);
    if (isMe9) {
      writeFlashMe9(bin, protocolId, flags);
    } else {
      writeFlashMe7(bin, protocolId, flags);
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

} // namespace flasher
