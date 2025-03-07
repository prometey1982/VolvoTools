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
  explicit D2FlasherBase(j2534::J2534 &j2534, unsigned long baudrate);
  ~D2FlasherBase();

  void canWakeUp(unsigned long baudrate);

protected:
  unsigned long getBaudrate() const;

  common::ECUType getEcuType(common::CMType cmType) const;

  void openChannels(unsigned long baudrate, bool additionalConfiguration);
  void resetChannels();

  void selectAndWriteBootloader(common::CMType cmType, unsigned long protocolId,
                                unsigned long flags);
  void canWakeUp();

  virtual common::VBF getSBL(common::CMType cmType) const;

  void canGoToSleep(unsigned long protocolId, unsigned long flags);
  void cleanErrors();

  void writeStartPrimaryBootloaderMsgAndCheckAnswer(common::ECUType ecuType,
                                                    unsigned long protocolId,
                                                    unsigned long flags);
  void writeDataOffsetAndCheckAnswer(common::ECUType ecuType,
                                     uint32_t writeOffset,
                                     unsigned long protocolId,
                                     unsigned long flags);
  void writeSBL(common::ECUType ecuType, const common::VBF &sbl,
                unsigned long protocolId, unsigned long flags);
  void writeChunk(common::ECUType ecuType, const std::vector<uint8_t> &bin,
                  uint32_t beginOffset, uint32_t endOffset,
                  unsigned long protocolId, unsigned long flags);
  void writeChunk(common::ECUType ecuType, const std::vector<uint8_t> &data,
                  uint32_t writeOffset, unsigned long protocolId, unsigned long flags);
  void eraseMemory(common::ECUType ecuType, uint32_t offset,
                   unsigned long protocolId, unsigned long flags,
                   uint8_t toCheck);
  void eraseMemory2(common::ECUType ecuType, uint32_t offset,
      unsigned long protocolId, unsigned long flags,
      uint8_t toCheck, uint8_t toCheck2);
  void writeFlashMe7(const std::vector<uint8_t> &bin, unsigned long protocolId,
                     unsigned long flags);
  void writeFlashMe9(const std::vector<uint8_t> &bin, unsigned long protocolId,
                     unsigned long flags);
  void writeFlashTCM(const std::vector<uint8_t> &bin, unsigned long protocolId,
                     unsigned long flags);

private:
  unsigned long _baudrate;
};

} // namespace flasher
