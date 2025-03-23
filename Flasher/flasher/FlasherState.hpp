#pragma once

namespace flasher {

enum class FlasherState {
    Initial,
    OpenChannels,
    FallAsleep,
    Authorize,
    ProgrammingSession,
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
