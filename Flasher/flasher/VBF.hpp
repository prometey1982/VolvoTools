#pragma once

#include "VBFChunk.hpp"

namespace flasher {

struct VBF {
  uint32_t jumpAddr;
  std::vector<VBFChunk> chunks;

  VBF(uint32_t jumpAddr, const std::vector<VBFChunk> &chunks)
      : jumpAddr(jumpAddr), chunks(chunks) {}
};

} // namespace flasher
