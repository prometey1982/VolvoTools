#pragma once

#include "CompressorBase.hpp"

namespace common {

class LZSSCompressor: public CompressorBase {
public:
    virtual std::vector<uint8_t> compress(const std::vector<uint8_t>& input) override;
    virtual std::vector<uint8_t> decompress(const std::vector<uint8_t>& input) override;
};

} // namespace common
