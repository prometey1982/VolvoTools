#pragma once

#include "FlasherCallback.hpp"

#include "FlasherBase.hpp"

#include <common/GenericProcess.hpp>
#include <common/CMType.hpp>
#include <common/D2Messages.hpp>
#include <common/VBF.hpp>

#include <condition_variable>
#include <mutex>
#include <sstream>
#include <thread>
#include <tuple>
#include <vector>

namespace flasher {

class D2FlasherBase: public FlasherBase {
public:
  explicit D2FlasherBase(common::J2534Info &j2534Info, FlasherParameters&& flasherParameters);
  ~D2FlasherBase();

  void canWakeUp(unsigned long baudrate);

protected:
  void selectAndWriteBootloader();
  void canWakeUp();

  void canGoToSleep();
  void cleanErrors();

  void writeStartPrimaryBootloaderMsgAndCheckAnswer(uint8_t ecuId);
  void writeDataOffsetAndCheckAnswer(uint8_t ecuId,
                                     uint32_t writeOffset);
  void writeSBL(uint8_t ecuId, const common::VBF &sbl);
  void writeChunk(uint8_t ecuId, const std::vector<uint8_t> &bin,
                  uint32_t beginOffset, uint32_t endOffset);
  void writeChunk(uint8_t ecuId, const std::vector<uint8_t> &data,
                  uint32_t writeOffset);
  void eraseMemory(uint8_t ecuId, uint32_t offset, uint8_t toCheck);
  void eraseMemory2(uint8_t ecuId, uint32_t offset,
      uint8_t toCheck, uint8_t toCheck2);
  void writeFlashMe7(const std::vector<uint8_t> &bin);
  void writeFlashMe9(const std::vector<uint8_t> &bin);
  void writeFlashTCM(const std::vector<uint8_t> &bin);
};

} // namespace flasher
