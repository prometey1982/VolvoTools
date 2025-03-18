#include "common/protocols/TP20Error.hpp"

namespace common {

namespace {

const char* getWhatString(uint8_t errorCode)
{
    switch(errorCode) {
    case TP20Error::ErrorCode::GenericError:
        return "Generic Error";
    case TP20Error::ErrorCode::ServiceNotSupportedInvalidFormat:
        return "Service not supported";
    case TP20Error::ErrorCode::SubFunctionNotSupported:
        return "Sub Function not supported";
    case TP20Error::ErrorCode::InvalidMessageOrLengthFormat:
        return "Invalid message length/format";
    case TP20Error::ErrorCode::BusyRepeatRequest:
        return "Busy-repeat request";
    case TP20Error::ErrorCode::ConditionsNotCorrect:
        return "Conditions not correct";
    case TP20Error::ErrorCode::RequestSequenceError:
        return "Request sequence error";
    case TP20Error::ErrorCode::RequestOutOfRange:
        return "Request out of range";
    case TP20Error::ErrorCode::SecurityAccessDenied:
        return "Security access denied";
    case TP20Error::ErrorCode::InvalidKey:
        return "Invalid Key";
    case TP20Error::ErrorCode::ExceedNumberOfAttempts:
        return "Exceeded number of attempts";
    case TP20Error::ErrorCode::RequiredTimeDelayHasNotExpired:
        return "Required time delay has not expired";
    case TP20Error::ErrorCode::ImproperDownloadType:
        return "Improper download type";
    case TP20Error::ErrorCode::CanNotDownloadToSpecifiedAddress:
        return "Can not download to specified address";
    case TP20Error::ErrorCode::CanNotDownloadNumberOfBytesRequested:
        return "Can not download number of bytes requested";
    case TP20Error::ErrorCode::UploadNotAccepted:
        return "Upload not accepted";
    case TP20Error::ErrorCode::ImproperUploadType:
        return "Improper upload type";
    case TP20Error::ErrorCode::CanNotUploadFromSpecifiedAddress:
        return "Can not upload from specified address";
    case TP20Error::ErrorCode::CanNotUploadNumberOfBytesRequested:
        return "Can not upload number of bytes requested";
    case TP20Error::ErrorCode::TransferDataSuspended:
        return "Transfer data suspended";
    case TP20Error::ErrorCode::TransferAborted:
        return "Transfer aborted";
    case TP20Error::ErrorCode::IllegalAddressInBlockTransfer:
        return "Illegal address in block transfer";
    case TP20Error::ErrorCode::IllegalByteCountInBlockTransfer:
        return "Illegal byte count in block transfer";
    case TP20Error::ErrorCode::IllegalBlockTransferType:
        return "Illegal block transfer type";
    case TP20Error::ErrorCode::BlockTransferDataChecksumError:
        return "Block transfer data checksum error";
    case TP20Error::ErrorCode::BusyResponsePending:
        return "Busy - response pending";
    case TP20Error::ErrorCode::IncorrectByteCountDuringBlockTransfer:
        return "Incorrect byte count during block transfer";
    case TP20Error::ErrorCode::SubFunctionNotSupportedInActiveSession:
        return "Sub function not supported in active session";
    case TP20Error::ErrorCode::ServiceOrSubfunctionNotSupported:
        return "Service or subfunction not supported";
    case TP20Error::ErrorCode::ServiceNotSupportedInActiveSession:
        return "Service not supported in active session";
    case TP20Error::ErrorCode::NoProgram:
        return "No program";
    }
    return "";
}

}

TP20Error::TP20Error(uint8_t errorCode)
    : std::runtime_error{getWhatString(errorCode)}
    , _errorCode{ errorCode }
{
}

uint8_t TP20Error::getErrorCode() const noexcept
{
    return _errorCode;
}

} // namespace common
