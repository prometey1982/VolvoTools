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

class D2Reader: public D2FlasherBase {
public:
  explicit D2Reader(common::J2534Info &j2534Info, FlasherParameters&& flasherParameters,
                    uint32_t startPos, uint32_t size, std::vector<uint8_t>& bin);
  ~D2Reader();

  void read(uint32_t startPos, uint32_t size, std::vector<uint8_t>& bin);

private:
  void startImpl() override;

  void readFunction(std::vector<uint8_t>& bin, uint32_t startPos, uint32_t size);

private:
  uint32_t _startPos;
  uint32_t _size;
  std::vector<uint8_t>& _bin;
};

} // namespace flasher
