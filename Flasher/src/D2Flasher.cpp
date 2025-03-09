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
D2Flasher::D2Flasher(common::J2534Info &j2534Info, FlasherParameters&& flasherParameters,
                     const common::VBF &bin)
    : D2FlasherBase{ j2534Info, std::move(flasherParameters) }
    , _bin{ bin }
{}

D2Flasher::~D2Flasher() { }

void D2Flasher::startImpl() {
    size_t maximumProgress = std::accumulate(_bin.chunks.cbegin(), _bin.chunks.cend(), static_cast<size_t>(0),
                                           [](const auto& value, const common::VBFChunk& chunk) {
                                               return value + chunk.data.size();
                                           });

  setMaximumProgress(maximumProgress);
  setCurrentProgress(0);

  writeFlash();
}

void D2Flasher::writeFlash() {
  const auto ecuId{ getFlasherParameters().ecuId };
  for(const auto& chunk: _bin.chunks) {
      eraseMemory2(ecuId, chunk.writeOffset, 0xF9, 0x0);
      writeChunk(ecuId, chunk.data, chunk.writeOffset);
  }
}

void D2Flasher::writeFlashMe7(const std::vector<uint8_t> &bin) {
  const auto ecuId{ getFlasherParameters().ecuId };
  eraseMemory(ecuId, 0x8000, 0xF9);
  std::this_thread::sleep_for(std::chrono::seconds(3));
  eraseMemory(ecuId, 0x10000, 0xF9);
  writeChunk(ecuId, bin, 0x8000, 0xE000);
  writeChunk(ecuId, bin, 0x10000, bin.size());
}

void D2Flasher::writeFlashMe9(const std::vector<uint8_t> &bin) {
  const auto ecuId{ getFlasherParameters().ecuId };
  eraseMemory2(ecuId, 0x20000, 0xF9, 0x0);
  writeChunk(ecuId, bin, 0x20000, 0x90000);
  writeChunk(ecuId, bin, 0xA0000, 0x1F0000);
}

void D2Flasher::writeFlashTCM(const std::vector<uint8_t> &bin) {
  const auto ecuId{ getFlasherParameters().ecuId };
  const std::vector<uint32_t> chunks{0x8000,  0x10000, 0x20000,
                                     0x30000, 0x40000, 0x50000,
                                     0x60000, 0x70000, 0x80000};
  for (size_t i = 0; i < chunks.size() - 1; ++i) {
    eraseMemory2(ecuId, chunks[i], 0xF9, 0x0);
    writeChunk(ecuId, bin, chunks[i], chunks[i + 1]);
    setCurrentProgress(chunks[i + 1]);
  }
}

void D2Flasher::flasherFunction(common::CMType cmType, const std::vector<uint8_t> bin) {
  try {
    selectAndWriteBootloader();
    switch (cmType) {
    case common::CMType::ECM_ME7:
      writeFlashMe7(bin);
      break;
    case common::CMType::ECM_ME9_P1:
      writeFlashMe9(bin);
      break;
    case common::CMType::TCM_AW55_P2:
    case common::CMType::TCM_TF80_P2:
      writeFlashTCM(bin);
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
