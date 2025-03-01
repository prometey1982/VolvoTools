#include "common/UDSError.hpp"

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
        return "Sub Function not supported";
    case 0x13:
        return "Invalid message length/format";
    case 0x14:
        return "Response too long";
    case 0x21:
        return "Busy-repeat request";
    case 0x22:
        return "Conditions not correct";
    case 0x24:
        return "Request sequence error";
    case 0x25:
        return "No response from subnet component";
    case 0x26:
        return "Failure prevents execution of requested action";
    case 0x31:
        return "Request out of range";
    case 0x33:
        return "Security access denied";
    case 0x35:
        return "Invalid Key";
    case 0x36:
        return "Exceeded number of attempts";
    case 0x37:
        return "Required time delay has not expired";
    case 0x70:
        return "Upload/download not accepted";
    case 0x71:
        return "Transfer data suspended";
    case 0x72:
        return "Programming failure";
    case 0x73:
        return "Wrong block sequence counter";
    case 0x78:
        return "Request received - response pending";
    case 0x7E:
        return "Sub function not supported in active session";
    case 0x7F:
        return "Service not supported in active session";
    case 0x81:
        return "RPM too high";
    case 0x82:
        return "RPM too low";
    case 0x83:
        return "Engine is running";
    case 0x84:
        return "Engine is not running";
    case 0x85:
        return "Engine run time too low";
    case 0x86:
        return "Temperature too high";
    case 0x87:
        return "Temperature too low";
    case 0x88:
        return "Speed too high";
    case 0x89:
        return "Speed too low";
    case 0x8A:
        return "Throttle pedal too high";
    case 0x8B:
        return "Throttle pedal too low";
    case 0x8C:
        return "Transmission range not in neutral";
    case 0x8D:
        return "Transmission range not in gear";
    case 0x8F:
        return "Brake switches not closed";
    case 0x90:
        return "Shifter lever not in park";
    case 0x91:
        return "Torque converter clutch locked";
    case 0x92:
        return "Voltage too high";
    case 0x93:
        return "Voltage too low";
    }
    return "";
}

}

UDSError::UDSError(uint8_t errorCode)
    : std::runtime_error{getWhatString(errorCode)}
    , _errorCode{ errorCode }
{
}

uint8_t UDSError::getErrorCode() const noexcept
{
    return _errorCode;
}

} // namespace common
