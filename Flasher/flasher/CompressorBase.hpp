#pragma once

#include <cinttypes>
#include <vector>

namespace flasher {

class CompressorBase {
public:
    virtual ~CompressorBase() = default;
    virtual std::vector<uint8_t> compress(const std::vector<uint8_t>& data) = 0;
    virtual std::vector<uint8_t> decompress(const std::vector<uint8_t>& data) = 0;
};

} // namespace flasher
