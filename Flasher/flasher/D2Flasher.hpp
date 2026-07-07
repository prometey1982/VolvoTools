#pragma once

#include "D2FlasherBase.hpp"

#include <common/GenericProcess.hpp>
#include <common/CMType.hpp>
#include <common/VBF.hpp>

#include <vector>

class ICanChannel;

namespace j2534 {
class J2534;
} // namespace j2534

namespace flasher {

class D2Flasher: public D2FlasherBase {
public:
  explicit D2Flasher(j2534::J2534 &j2534,
      FlasherParameters&& flasherParameters);
  ~D2Flasher();

private:
  virtual size_t getMaximumFlashProgress() const override;
  virtual bool isBootloaderRequired() const override;
  virtual void eraseStep(ICanChannel &channel, uint8_t ecuId) override;
  virtual void writeStep(ICanChannel &channel, uint8_t ecuId) override;
};

} // namespace flasher
