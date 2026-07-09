#pragma once

#include "SBLProviderBase.hpp"
#include "common/VBF.hpp"
#include "common/compression/CompressionType.hpp"
#include "common/encryption/EncryptionType.hpp"

#include <array>
#include <cstdint>
#include <memory>

namespace flasher {

struct D2FlasherConfig {
    common::VBF bootloader;
    const common::VBF flash;
};

struct UDSFlasherConfig {
    std::array<uint8_t, 5> pin;
    common::VBF bootloader;
    const common::VBF flash;
};

struct KWPFlasherConfig {
    common::VBF bootloader;
    std::array<uint8_t, 5> pin;
    const common::VBF flash;
    common::CompressionType compressionType;
};

struct VAGFlasherConfig {
    std::array<uint8_t, 5> pin;
    const common::VBF flash;
    common::CompressionType compressionType;
    common::EncryptionType encryptionType;
};

} // namespace flasher
