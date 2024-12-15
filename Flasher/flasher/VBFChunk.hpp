#pragma once

#include <cstdint>
#include <vector>

namespace flasher {

struct VBFChunk {
  uint32_t writeOffset;
  std::vector<uint8_t> data;
  uint32_t crc;

  VBFChunk(uint32_t writeOffset, const std::vector<uint8_t> data)
      : writeOffset(writeOffset), data(data), crc() {}
};

} // namespace flasher
