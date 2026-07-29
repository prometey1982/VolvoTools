#pragma once

#include <cstdint>

namespace common {

enum class ProtocolType : uint32_t {
    CAN      = 0x5,
    ISO15765 = 0x6,
    ISO14230 = 0x4,
    ISO9141  = 0x3,
    TP20     = 0x8002,
};

} // namespace common
