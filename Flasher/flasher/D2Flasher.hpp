#pragma once

#include "FlasherCallback.hpp"

#include "D2FlasherBase.hpp"

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

namespace j2534 {
class J2534;
class J2534Channel;
} // namespace j2534

namespace flasher {

class D2Flasher: public D2FlasherBase {
public:
  explicit D2Flasher(common::J2534Info &j2534Info, 
      FlasherParameters&& flasherParameters, const common::VBF &bin);
  ~D2Flasher();

private:
  void startImpl() override;

  void writeFlash();

  void writeFlashMe7(const std::vector<uint8_t> &bin);
  void writeFlashMe9(const std::vector<uint8_t> &bin);
  void writeFlashTCM(const std::vector<uint8_t> &bin);

  void flasherFunction(common::CMType cmType, const std::vector<uint8_t> bin);

private:
  const common::VBF _bin;
};

} // namespace flasher
