#pragma once

#include <cstdint>
#include <vector>

namespace common {

struct VBFChunk {
  uint32_t writeOffset;
  std::vector<uint8_t> data;
  uint32_t crc;

  VBFChunk(uint32_t writeOffset, const std::vector<uint8_t>& data, uint32_t crc = {})
      : writeOffset(writeOffset), data(data), crc(crc) {}
  VBFChunk(uint32_t writeOffset, std::vector<uint8_t>&& data, uint32_t crc = {})
      : writeOffset(writeOffset), data(std::move(data)), crc(crc) {
  }
};

} // namespace common
