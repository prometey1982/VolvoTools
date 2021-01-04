#include "Flasher.hpp"

#include "../Common/Util.hpp"
#include "../common/CEMCanMessage.hpp"
#include "../common/CanMessages.hpp"
#include "../j2534/J2534.hpp"
#include "../j2534/J2534Channel.hpp"

#include <algorithm>

namespace flasher {
Flasher::Flasher(j2534::J2534 &j2534)
    : _j2534{j2534}, _currentState{State::Initial} {}

Flasher::~Flasher() { stop(); }

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

  const unsigned long protocolId = CAN_XON_XOFF;
  const unsigned long flags = CAN_29BIT_ID;

  _channel1 = common::openChannel(_j2534, protocolId, flags, baudrate);
  if (baudrate != 500000)
    _channel2 = common::openBridgeChannel(_j2534);

  setState(State::InProgress);

  _flasherThread = std::thread([this, bin, protocolId, flags] {
    flasherFunction(bin, protocolId, flags);
  });
}

void Flasher::stop() {}

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
            ISO9141, ISO9141_K_LINE_ONLY),
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
      {common::CanMessages::wakeUpCanRequest.toPassThruMsg(protocolId, flags)},
      numMsgs);
  if (_channel2) {
    _channel2->writeMsgs({common::CanMessages::wakeUpCanRequest.toPassThruMsg(
                             protocolId, flags)},
                         numMsgs);
  }
}

void Flasher::writePreFlashMsgAndCheckAnswer(unsigned long protocolId,
                                             unsigned long flags) {
  unsigned long msgsNum = 1;
  _channel1->writeMsgs(
      {common::CanMessages::wakeUpECM.toPassThruMsg(protocolId, flags)},
      msgsNum);
  std::vector<PASSTHRU_MSG> msgs(1);
  _channel1->readMsgs(msgs);
  for (const auto &msg : msgs) {
    if (msg.Data[5] == 0xC6) {
      return;
    }
  }
  throw std::runtime_error("ECM didn't response with correct answer");
}

void Flasher::writeBootloader(uint32_t writeOffset,
                              const std::vector<uint8_t> &bootloader,
                              unsigned long protocolId, unsigned long flags) {
  auto bootloaderMsgs =
      common::CanMessages::createWriteDataMsgs(bootloader, protocolId, flags);
  unsigned long numMsgs = 1;
  _channel1->writeMsgs({common::CanMessages::createWriteOffsetMsg(writeOffset)
                            .toPassThruMsg(protocolId, flags)},
                       numMsgs);
  _channel1->writeMsgs(bootloaderMsgs, numMsgs, 240000);
  _channel1->writeMsgs({common::CanMessages::createWriteOffsetMsg(writeOffset)
                            .toPassThruMsg(protocolId, flags)},
                       numMsgs);
  _channel1->writeMsgs(
      {common::CanMessages::restartECMMsg.toPassThruMsg(protocolId, flags)},
      numMsgs);
}

void Flasher::writeChunk(const std::vector<uint8_t> &bin, uint32_t beginOffset,
                         uint32_t endOffset, unsigned long protocolId,
                         unsigned long flags) {
  auto binMsgs = common::CanMessages::createWriteDataMsgs(
      bin, beginOffset, endOffset, protocolId, flags);
  unsigned long numMsgs = 1;
  _channel1->writeMsgs({common::CanMessages::createWriteOffsetMsg(beginOffset)
                            .toPassThruMsg(protocolId, flags)},
                       numMsgs);
  _channel1->writeMsgs(binMsgs, numMsgs, 240000);
}

void Flasher::writeFlashMe7(const std::vector<uint8_t> &bin,
                            unsigned long protocolId, unsigned long flags) {
  writeChunk(bin, 0x8000, 0xE000, protocolId, flags);
  writeChunk(bin, 0x10000, bin.size(), protocolId, flags);
}

void Flasher::writeFlashMe9(const std::vector<uint8_t> &bin,
                            unsigned long protocolId, unsigned long flags) {
  writeChunk(bin, 0x20000, 0x90000, protocolId, flags);
  writeChunk(bin, 0xA0000, 0x1F0000, protocolId, flags);
}

void Flasher::flasherFunction(const std::vector<uint8_t> bin,
                              unsigned long protocolId, unsigned long flags) {
  try {
    unsigned long msgsNum = 1;
    _channel1->writeMsgs(
        {common::CanMessages::wakeUpECM.toPassThruMsg(protocolId, flags)},
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
  } catch (...) {
    canWakeUp(protocolId, flags);
    setState(State::Error);
  }
}

void Flasher::setState(State newState) {
  std::unique_lock<std::mutex> lock{_mutex};
  _currentState = newState;
}

} // namespace flasher
