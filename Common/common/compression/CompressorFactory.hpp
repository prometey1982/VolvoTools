#pragma once

#include "CompressionType.hpp"

#include <memory>

namespace common {

class CompressorBase;

class CompressorFactory {
public:
    static std::unique_ptr<CompressorBase> create(CompressionType compressionType);
};

} // namespace common
