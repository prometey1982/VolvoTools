#include "flasher/D2Flasher.hpp"

#include <common/Util.hpp>
#include <common/D2Message.hpp>
#include <common/SBL.hpp>
#include <common/VBFParser.hpp>
#include <j2534/J2534.hpp>
#include <j2534/J2534Channel.hpp>

#include <algorithm>
#include <numeric>
#include <time.h>

namespace flasher {
D2Flasher::D2Flasher(j2534::J2534 &j2534, unsigned long baudrate, common::CMType cmType,
                     const common::VBF &bin)
    : D2FlasherBase{ j2534, baudrate }
    , _cmType{ cmType }
    , _bin{ bin }
{}

D2Flasher::~D2Flasher() { }

void D2Flasher::startImpl() {
  openChannels(getBaudrate(), true);

    size_t maximumProgress = std::accumulate(_bin.chunks.cbegin(), _bin.chunks.cend(), static_cast<size_t>(0),
                                           [](const auto& value, const common::VBFChunk& chunk) {
                                               return value + chunk.data.size();
                                           });

  setMaximumProgress(maximumProgress);
  setCurrentProgress(0);

  writeFlash();
}

void D2Flasher::writeFlash() {
  const unsigned long protocolId = CAN;
  const unsigned long flags = CAN_29BIT_ID;

  const auto ecuType = getEcuType(_cmType);
  for(const auto& chunk: _bin.chunks) {
      eraseMemory2(ecuType, chunk.writeOffset, protocolId, flags, 0xF9, 0x0);
      writeChunk(ecuType, chunk.data, chunk.writeOffset, protocolId, flags);
  }
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
  eraseMemory2(ecuType, 0x20000, protocolId, flags, 0xF9, 0x0);
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
    eraseMemory2(ecuType, chunks[i], protocolId, flags, 0xF9, 0x0);
    writeChunk(ecuType, bin, chunks[i], chunks[i + 1], protocolId, flags);
    setCurrentProgress(chunks[i + 1]);
  }
}

void D2Flasher::flasherFunction(common::CMType cmType, const std::vector<uint8_t> bin,
                                unsigned long protocolId, unsigned long flags) {
  try {
    selectAndWriteBootloader(cmType, protocolId, flags);
    switch (cmType) {
    case common::CMType::ECM_ME7:
      writeFlashMe7(bin, protocolId, flags);
      break;
    case common::CMType::ECM_ME9_P1:
      writeFlashMe9(bin, protocolId, flags);
      break;
    case common::CMType::TCM_AW55_P2:
    case common::CMType::TCM_TF80_P2:
      writeFlashTCM(bin, protocolId, flags);
      break;
    }
    canWakeUp();
    setCurrentState(FlasherState::Done);
  } catch (std::exception &ex) {
    canWakeUp();
    setCurrentState(FlasherState::Error);
  }
}

} // namespace flasher
