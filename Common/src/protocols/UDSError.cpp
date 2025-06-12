#include "common/protocols/UDSError.hpp"

namespace common {

namespace {

const char* getWhatString(uint8_t errorCode)
{
    switch(errorCode) {
    case UDSError::ErrorCode::GenericError:
        return "Generic Error";
    case UDSError::ErrorCode::ServiceNotSupported:
        return "Service not supported";
    case UDSError::ErrorCode::SubFunctionNotSupported:
        return "Sub Function not supported";
    case UDSError::ErrorCode::InvalidMessageOrLengthFormat:
        return "Invalid message length/format";
    case UDSError::ErrorCode::ResponseTooLong:
        return "Response too long";
    case UDSError::ErrorCode::BusyRepeatRequest:
        return "Busy-repeat request";
    case UDSError::ErrorCode::ConditionsNotCorrect:
        return "Conditions not correct";
    case UDSError::ErrorCode::RequestSequenceError:
        return "Request sequence error";
    case UDSError::ErrorCode::NoResponseFromSubnetComponent:
        return "No response from subnet component";
    case UDSError::ErrorCode::FailurePreventsExecutionOfRequestedAction:
        return "Failure prevents execution of requested action";
    case UDSError::ErrorCode::RequestOutOfRange:
        return "Request out of range";
    case UDSError::ErrorCode::SecurityAccessDenied:
        return "Security access denied";
    case UDSError::ErrorCode::InvalidKey:
        return "Invalid Key";
    case UDSError::ErrorCode::ExceedNumberOfAttempts:
        return "Exceeded number of attempts";
    case UDSError::ErrorCode::RequiredTimeDelayHasNotExpired:
        return "Required time delay has not expired";
    case UDSError::ErrorCode::UploadDownloadNotAccepted:
        return "Upload/download not accepted";
    case UDSError::ErrorCode::TransferDataSuspended:
        return "Transfer data suspended";
    case UDSError::ErrorCode::ProgrammingFailure:
        return "Programming failure";
    case UDSError::ErrorCode::WrongBlockSequenceCounter:
        return "Wrong block sequence counter";
    case UDSError::ErrorCode::RequestReceivedResponsePending:
        return "Request received - response pending";
    case UDSError::ErrorCode::SubFunctionNotSupportedInActiveSession:
        return "Sub function not supported in active session";
    case UDSError::ErrorCode::ServiceNotSupportedInActiveSession:
        return "Service not supported in active session";
    case UDSError::ErrorCode::RPMTooHigh:
        return "RPM too high";
    case UDSError::ErrorCode::RPMTooLow:
        return "RPM too low";
    case UDSError::ErrorCode::EngineIsRunning:
        return "Engine is running";
    case UDSError::ErrorCode::EngineIsNotRunning:
        return "Engine is not running";
    case UDSError::ErrorCode::EngineRunTimeTooLow:
        return "Engine run time too low";
    case UDSError::ErrorCode::TemperatureTooHigh:
        return "Temperature too high";
    case UDSError::ErrorCode::TemperatureTooLow:
        return "Temperature too low";
    case UDSError::ErrorCode::SpeedTooHigh:
        return "Speed too high";
    case UDSError::ErrorCode::SpeedTooLow:
        return "Speed too low";
    case UDSError::ErrorCode::ThrottlePedalTooHigh:
        return "Throttle pedal too high";
    case UDSError::ErrorCode::ThrottlePedalTooLow:
        return "Throttle pedal too low";
    case UDSError::ErrorCode::TransmissionRangeNotInNeutral:
        return "Transmission range not in neutral";
    case UDSError::ErrorCode::TransmissionRangeNotInGear:
        return "Transmission range not in gear";
    case UDSError::ErrorCode::BrakeSwitchesNotClosed:
        return "Brake switches not closed";
    case UDSError::ErrorCode::ShifterLevelNotInPark:
        return "Shifter lever not in park";
    case UDSError::ErrorCode::TorqueConverterClutchLocked:
        return "Torque converter clutch locked";
    case UDSError::ErrorCode::VoltageTooHigh:
        return "Voltage too high";
    case UDSError::ErrorCode::VoltageTooLow:
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
