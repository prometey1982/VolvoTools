#pragma once

#include "SBLProviderBase.hpp"
#include "common/VBF.hpp"
#include "common/compression/CompressionType.hpp"
#include "common/encryption/EncryptionType.hpp"

#include <cstdint>
#include <memory>
#include <vector>

namespace flasher {

struct ReadRange {
    uint32_t startAddr;
    size_t size;
};

using ReadRanges = std::vector<ReadRange>;

struct AuthorizationParams {
    uint64_t pin;
};

struct BootloaderParams {
    std::shared_ptr<SBLProviderBase> sblProvider;
};

struct EncryptionParams {
    common::CompressionType compression;
    common::EncryptionType encryption;
};

} // namespace flasher
