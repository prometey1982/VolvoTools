#pragma once

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

} // namespace flasher
