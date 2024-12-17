#pragma once

#include <string>

namespace flasher {

enum class FlasherStep {
    OpenChannels,
    FallingAsleep,
    Authorizing,
    BootloaderLoading,
    FlashErasing,
    FlashLoading,
    WakeUp,
    CloseChannels
};

std::string toString(FlasherStep step);

} // namespace flasher
