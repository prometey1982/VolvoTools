#include "common/protocols/D2Error.hpp"

namespace common {

namespace {

const char* getWhatString(uint8_t errorCode)
{
    switch(errorCode) {
    case 0x10:
        return "Generic Error";
    case 0x11:
        return "Service not supported";
    case 0x12:
        return "Sub Function not supported or invalid message format";
    case 0x21:
        return "Busy - repeat request";
    case 0x22:
        return "Conditions not correct";
    case 0x23:
        return "Request action not yet completed";
    case 0x31:
        return "Request out of range";
    case 0x33:
        return "Security access denied";
    case 0x63:
        return "Abnormal stop";
    case 0x80:
        return "Access level to low";
    case 0x81:
        return "Busy bus";
    case 0x82:
        return "DTC(-s) had been stored again";
    case 0x83:
        return "Memory not erased";
    case 0x84:
        return "Abnormal stop";
    case 0x85:
        return "Request action not yet completed";
    case 0x86:
        return "Request action not yet completed";
    }
    return "";
}

}

D2Error::D2Error(uint8_t errorCode)
    : std::runtime_error{getWhatString(errorCode)}
    , _errorCode{ errorCode }
{
}

uint8_t D2Error::getErrorCode() const noexcept
{
    return _errorCode;
}

} // namespace common
