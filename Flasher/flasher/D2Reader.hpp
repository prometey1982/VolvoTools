#pragma once

#include "D2FlasherBase.hpp"

#include <common/GenericProcess.hpp>
#include <common/CMType.hpp>
#include <common/D2Messages.hpp>
#include <common/VBF.hpp>

#include <vector>

namespace j2534 {
class J2534;
class J2534Channel;
} // namespace j2534

namespace flasher {

class D2Reader: public D2FlasherBase {
public:
  explicit D2Reader(j2534::J2534 &j2534, FlasherParameters&& flasherParameters,
                    uint32_t startPos, uint32_t size, std::vector<uint8_t>& bin);
  ~D2Reader();

private:
  virtual size_t getMaximumFlashProgress() const override;
  virtual void eraseStep(j2534::J2534Channel &channel, uint8_t ecuId) override;
  virtual void writeStep(j2534::J2534Channel &channel, uint8_t ecuId) override;

private:
  uint32_t _startPos;
  uint32_t _size;
  std::vector<uint8_t>& _bin;
};

} // namespace flasher
