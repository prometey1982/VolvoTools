#include "flasher/FlasherStep.hpp"

namespace flasher {

std::string toString(FlasherStep step)
{
    switch (step) {
    case FlasherStep::OpenChannels:
        return "Open channels";
    case FlasherStep::FallingAsleep:
        return "Falling asleep";
    case FlasherStep::Authorizing:
        return "Authorizing";
    case FlasherStep::BootloaderLoading:
        return "Bootloader loading";
    case FlasherStep::FlashErasing:
        return "Flash erasing";
    case FlasherStep::FlashLoading:
        return "Flash loading";
    case FlasherStep::WakeUp:
        return "Wake up";
    case FlasherStep::CloseChannels:
        return "Close channels";
    }
    return {};
}

} // namespace flasher
