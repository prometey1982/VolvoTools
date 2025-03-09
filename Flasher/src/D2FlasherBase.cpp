#include "flasher/D2FlasherBase.hpp"

#include <common/Util.hpp>
#include <common/D2Message.hpp>
#include <common/SBL.hpp>
#include <common/VBFParser.hpp>
#include <j2534/J2534.hpp>
#include <j2534/J2534Channel.hpp>

#include <algorithm>
#include <map>
#include <numeric>
#include <time.h>

namespace {

bool writeMessagesAndCheckAnswer(j2534::J2534Channel& channel,
                                 const j2534::BaseMessage& message,
                                 const std::vector<uint8_t>& toCheck, size_t count = 10) {
  channel.clearRx();
  unsigned long msgsNum = 1;
  const auto error = channel.writeMsgs(message, msgsNum, 5000);
  if (error != STATUS_NOERROR) {
    throw std::runtime_error("write msgs error");
  }
  std::vector<PASSTHRU_MSG> received_msgs(1);
  for (size_t i = 0; i < count; ++i) {
    channel.readMsgs(received_msgs, 3000);
    for (const auto &msg : received_msgs) {
      for(size_t i = 0; i < toCheck.size(); ++i) {
        if(toCheck[i] != msg.Data[i + 5])
          return false;
      }
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
D2FlasherBase::D2FlasherBase(common::J2534Info &j2534Info, FlasherParameters&& flasherParameters)
    : FlasherBase{ j2534Info, std::move(flasherParameters) }
{}

D2FlasherBase::~D2FlasherBase() { }

void D2FlasherBase::canWakeUp(unsigned long baudrate) {
  canWakeUp();
  cleanErrors();
}

void D2FlasherBase::selectAndWriteBootloader() {
  uint32_t bootloaderOffset{ 0 };
  const auto ecuId{ getFlasherParameters().ecuId };
  const auto carPlatform{ getFlasherParameters().carPlatform };
  const auto additionalData{ getFlasherParameters().additionalData };

  const auto bootloader = getFlasherParameters().sblProvider->getSBL(carPlatform, ecuId, additionalData);
  if (bootloader.chunks.empty())
    throw std::runtime_error("Secondary bootloader not found");

  auto& channel = getJ2534Info().getChannelForEcu(ecuId);

  unsigned long msgsNum = 1;
  channel.writeMsgs(common::D2Messages::createWakeUpECUMsg(ecuId), msgsNum);

  canGoToSleep();
  std::this_thread::sleep_for(std::chrono::seconds(1));
  writeStartPrimaryBootloaderMsgAndCheckAnswer(ecuId);
  std::this_thread::sleep_for(std::chrono::seconds(1));
  writeSBL(ecuId, bootloader);
  std::this_thread::sleep_for(std::chrono::seconds(1));
}

void D2FlasherBase::canGoToSleep() {
  setCurrentState(FlasherState::FallAsleep);
  const auto ecuId{ getFlasherParameters().ecuId };
  auto& channels = getJ2534Info().getChannels();
  std::map<size_t, std::vector<unsigned long>> msgIds;
  for (size_t i = 0; i < channels.size(); ++i) {
      if (channels[i]->getProtocolId() != ISO9141) {
          msgIds[i] = channels[i]->startPeriodicMsgs(
              common::D2Messages::goToSleepCanRequest, 5);
      }
  }
  std::this_thread::sleep_for(std::chrono::seconds(3));
  for (const auto& ids : msgIds) {
      channels[ids.first]->stopPeriodicMsg(ids.second);
  }
}

void D2FlasherBase::canWakeUp() {
  setCurrentState(FlasherState::WakeUp);
  unsigned long numMsgs = 1;
  auto& channels = getJ2534Info().getChannels();
  for (size_t i = 0; i < channels.size(); ++i) {
      if (channels[i]->getProtocolId() != ISO9141) {
          channels[i]->writeMsgs(common::D2Messages::wakeUpCanRequest, numMsgs, 5000);
      }
  }

  std::this_thread::sleep_for(std::chrono::seconds(2));

  const auto now{std::chrono::system_clock::now()};
  const auto time_t = std::chrono::system_clock::to_time_t(now);
  struct tm lt;
  localtime_s(&lt, &time_t);

  auto& channel = getJ2534Info().getChannelForEcu(static_cast<uint8_t>(common::ECUType::DIM));
  channel.writeMsgs(
        common::D2Messages::setCurrentTime(lt.tm_hour, lt.tm_min), numMsgs,
        5000);
}

void D2FlasherBase::cleanErrors() {
  for (const auto ecuId :
       {static_cast<uint8_t>(common::ECUType::ECM_ME), static_cast<uint8_t>(common::ECUType::TCM), static_cast<uint8_t>(common::ECUType::SRS)}) {
    unsigned long numMsgs = 1;
    auto& channel = getJ2534Info().getChannelForEcu(ecuId);
    channel.writeMsgs(common::D2Messages::clearDTCMsgs(ecuId), numMsgs);
  }
}
void D2FlasherBase::writeStartPrimaryBootloaderMsgAndCheckAnswer(uint8_t ecuId) {
    auto& channel = getJ2534Info().getChannelForEcu(ecuId);
    if (!writeMessagesAndCheckAnswer(
          channel,
          common::D2Messages::createStartPrimaryBootloaderMsg(ecuId),
          { 0xC6 }))
    throw std::runtime_error("CM didn't response with correct answer");
}

void D2FlasherBase::writeDataOffsetAndCheckAnswer(uint8_t ecuId,
                                              uint32_t writeOffset) {
  auto& channel = getJ2534Info().getChannelForEcu(ecuId);
  const auto writeOffsetMsgs{
      common::D2Messages::createSetMemoryAddrMsg(ecuId, writeOffset)};
  for (int i = 0; i < 10; ++i) {
    if (writeMessagesAndCheckAnswer(channel, writeOffsetMsgs, { 0x9C }))
      return;
    std::this_thread::sleep_for(std::chrono::seconds(1));
  }
  throw std::runtime_error("CM didn't response with correct answer");
}

void D2FlasherBase::writeSBL(uint8_t ecuId, const common::VBF &bootloader) {
  setCurrentState(FlasherState::LoadBootloader);
  auto& channel = getJ2534Info().getChannelForEcu(ecuId);
  for (size_t i = 0; i < bootloader.chunks.size(); ++i) {
    const auto &chunk = bootloader.chunks[i];
    auto bootloaderMsgs =
        common::D2Messages::createWriteDataMsgs(ecuId, chunk.data);
    writeDataOffsetAndCheckAnswer(ecuId, chunk.writeOffset);
    for (auto message : bootloaderMsgs) {
      const auto passThruMsgs = message.toPassThruMsgs(channel.getProtocolId(), channel.getTxFlags());
      unsigned long numMsgs = passThruMsgs.size();
      channel.writeMsgs(passThruMsgs, numMsgs, 240000);
      if (numMsgs != passThruMsgs.size())
        throw std::runtime_error("Bootloader writing failed");
    }
    unsigned long numMsgs = 1;
    channel.writeMsgs(
        common::D2Messages::createSBLTransferCompleteMsg(ecuId),
        numMsgs);
    writeDataOffsetAndCheckAnswer(ecuId, chunk.writeOffset);
    if (!writeMessagesAndCheckAnswer(
            channel,
            common::D2Messages::createCalculateChecksumMsg(
                ecuId, chunk.writeOffset + chunk.data.size()),
            { 0xB1 }))
      throw std::runtime_error("Can't read memory after bootloader");
  }
  setCurrentState(FlasherState::StartBootloader);
  writeDataOffsetAndCheckAnswer(ecuId, bootloader.header.call);
  if (!writeMessagesAndCheckAnswer(
          channel,
          common::D2Messages::createJumpToMsg(ecuId),
          { 0xA0 }))
    throw std::runtime_error("Can't start bootloader");
}

void D2FlasherBase::writeChunk(uint8_t ecuId,
                           const std::vector<uint8_t> &bin,
                           uint32_t beginOffset, uint32_t endOffset) {
  setCurrentState(FlasherState::WriteFlash);
  auto storedProgress = getCurrentProgress();
  auto binMsgs = common::D2Messages::createWriteDataMsgs(
      ecuId, bin, beginOffset, endOffset);
  writeDataOffsetAndCheckAnswer(ecuId, beginOffset);
  auto& channel = getJ2534Info().getChannelForEcu(ecuId);
  for (const auto binMsg : binMsgs) {
    channel.clearRx();
    const auto passThruMsgs = binMsg.toPassThruMsgs(channel.getProtocolId(), channel.getTxFlags());
    unsigned long msgsNum = passThruMsgs.size();
    const auto error = channel.writeMsgs(passThruMsgs, msgsNum, 50000);
    if (error != STATUS_NOERROR) {
      throw std::runtime_error("write msgs error");
    }
    storedProgress += 6 * passThruMsgs.size();
    setCurrentProgress(storedProgress);
  }
  setCurrentProgress(endOffset);
  writeDataOffsetAndCheckAnswer(ecuId, beginOffset);
  uint8_t checksum = calculateCheckSum(bin, beginOffset, endOffset);
  if (!writeMessagesAndCheckAnswer(
          channel,
          common::D2Messages::createCalculateChecksumMsg(ecuId, endOffset),
          { 0xB1, checksum }))
    throw std::runtime_error("Failed. Checksums are not equal.");
}

void D2FlasherBase::writeChunk(uint8_t ecuId,
                               const std::vector<uint8_t> &bin,
                               uint32_t writeOffset) {
  setCurrentState(FlasherState::WriteFlash);
  auto storedProgress = getCurrentProgress();
  auto binMsgs = common::D2Messages::createWriteDataMsgs(
      ecuId, bin, 0, bin.size());
  writeDataOffsetAndCheckAnswer(ecuId, writeOffset);
  auto& channel = getJ2534Info().getChannelForEcu(ecuId);
  for (const auto binMsg : binMsgs) {
    channel.clearRx();
    const auto passThruMsgs = binMsg.toPassThruMsgs(channel.getProtocolId(), channel.getTxFlags());
    unsigned long msgsNum = passThruMsgs.size();
    const auto error = channel.writeMsgs(passThruMsgs, msgsNum, 50000);
    if (error != STATUS_NOERROR) {
      throw std::runtime_error("write msgs error");
    }
    storedProgress += 6 * passThruMsgs.size();
    setCurrentProgress(storedProgress);
  }
  const uint32_t endOffset = writeOffset + bin.size();
  setCurrentProgress(endOffset);
  writeDataOffsetAndCheckAnswer(ecuId, writeOffset);
  uint8_t checksum = calculateCheckSum(bin, writeOffset, endOffset);
  if (!writeMessagesAndCheckAnswer(
          channel,
          common::D2Messages::createCalculateChecksumMsg(ecuId, endOffset),
          { 0xB1, checksum }))
    throw std::runtime_error("Failed. Checksums are not equal.");
}

void D2FlasherBase::eraseMemory(uint8_t ecuId, uint32_t offset,
                            uint8_t toCheck) {
  writeDataOffsetAndCheckAnswer(ecuId, offset);
  auto& channel = getJ2534Info().getChannelForEcu(ecuId);
  if (!writeMessagesAndCheckAnswer(
          channel,
          common::D2Messages::createEraseMsg(ecuId),
          { toCheck }, 30))
    throw std::runtime_error("Can't erase memory");
}

void D2FlasherBase::eraseMemory2(uint8_t ecuId, uint32_t offset,
    uint8_t toCheck, uint8_t toCheck2) {
    writeDataOffsetAndCheckAnswer(ecuId, offset);
    auto& channel = getJ2534Info().getChannelForEcu(ecuId);
    if (!writeMessagesAndCheckAnswer(
        channel,
        common::D2Messages::createEraseMsg(ecuId),
        { toCheck, toCheck2 }, 30))
        throw std::runtime_error("Can't erase memory");
}

void D2FlasherBase::writeFlashMe7(const std::vector<uint8_t> &bin) {
  const uint8_t ecuId = static_cast<uint8_t>(common::ECUType::ECM_ME);
  eraseMemory(ecuId, 0x8000, 0xF9);
  std::this_thread::sleep_for(std::chrono::seconds(3));
  eraseMemory(ecuId, 0x10000, 0xF9);
  writeChunk(ecuId, bin, 0x8000, 0xE000);
  writeChunk(ecuId, bin, 0x10000, bin.size());
}

void D2FlasherBase::writeFlashMe9(const std::vector<uint8_t> &bin) {
  const uint8_t ecuId = static_cast<uint8_t>(common::ECUType::ECM_ME);
  eraseMemory2(ecuId, 0x20000, 0xF9, 0x0);
  writeChunk(ecuId, bin, 0x20000, 0x90000);
  writeChunk(ecuId, bin, 0xA0000, 0x1F0000);
}

void D2FlasherBase::writeFlashTCM(const std::vector<uint8_t> &bin) {
  const uint8_t ecuId = static_cast<uint8_t>(common::ECUType::TCM);
  const std::vector<uint32_t> chunks{0x8000,  0x10000, 0x20000,
                                     0x30000, 0x40000, 0x50000,
                                     0x60000, 0x70000, 0x80000};
  for (size_t i = 0; i < chunks.size() - 1; ++i) {
    eraseMemory(ecuId, chunks[i], 0xF9);
    writeChunk(ecuId, bin, chunks[i], chunks[i + 1]);
    setCurrentProgress(chunks[i + 1]);
  }
}

} // namespace flasher
