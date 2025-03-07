#pragma once

namespace flasher {

enum class FlasherState {
    Initial,
    OpenChannels,
    FallAsleep,
    Authorize,
    LoadBootloader,
    StartBootloader,
    EraseFlash,
    WriteFlash,
    ReadFlash,
    WakeUp,
    CloseChannels,
    Done,
    Error
};

}
