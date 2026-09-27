#pragma once

#include "compression/CompressionType.hpp"
#include "encryption/EncryptionType.hpp"
#include "ProtocolType.hpp"

#include <string>

namespace common {

struct ECUInfo {
    uint32_t ecuId;
    uint32_t canId;
    std::string name;
    CompressionType compressionType;
    EncryptionType encryptionType;
    ProtocolType protocol;
};

} // namespace common
