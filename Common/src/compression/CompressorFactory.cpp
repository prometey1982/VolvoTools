#include "common/compression/CompressorFactory.hpp"

#include "common/compression/BoschCompressor.hpp"
#include "common/compression/LZSSCompressor.hpp"

namespace common {

std::unique_ptr<CompressorBase> CompressorFactory::create(CompressionType compressionType)
{
    switch(compressionType) {
    case CompressionType::Bosch:
        return std::make_unique<BoschCompressor>();
    case CompressionType::LZSS:
        return std::make_unique<LZSSCompressor>();
    case CompressionType::None:
        return {};
    }
    return {};
}

} // namespace common
