#include "common/protocols/UDSStepType.hpp"

namespace common {

std::string toString(UDSStepType stepType)
{
    switch (stepType) {
    case UDSStepType::OpenChannels:
        return "Open channels";
    case UDSStepType::FallingAsleep:
        return "Falling asleep";
    case UDSStepType::Authorizing:
        return "Authorizing";
    case UDSStepType::BootloaderLoading:
        return "Bootloader loading";
    case UDSStepType::FlashErasing:
        return "Flash erasing";
    case UDSStepType::FlashLoading:
        return "Flash loading";
    case UDSStepType::WakeUp:
        return "Wake up";
    case UDSStepType::CloseChannels:
        return "Close channels";
    }
    return {};
}

} // namespace common
