#pragma once

#include <string>

namespace common {

enum class UDSStepType {
    OpenChannels,
    FallingAsleep,
    Authorizing,
    BootloaderLoading,
    BootloaderStarting,
    FlashErasing,
    FlashLoading,
    WakeUp,
    CloseChannels
};

std::string toString(UDSStepType stepType);

} // namespace common
