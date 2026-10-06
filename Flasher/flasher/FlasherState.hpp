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
    RequestDownload,
    EraseFlash,
    WriteFlash,
    CheckFlash,
    ReadFlash,
    WakeUp,
    CloseChannels,
    Done,
    Error
};

}
