#pragma once

#include <stdexcept>


namespace common {

class TP20Error: public std::runtime_error {
public:
    struct ErrorCode {
        static const int GenericError = 0x10;
        static const int ServiceNotSupportedInvalidFormat = 0x11;
        static const int SubFunctionNotSupported = 0x12;
        static const int InvalidMessageOrLengthFormat = 0x13;
        static const int BusyRepeatRequest = 0x21;
        static const int ConditionsNotCorrect = 0x22;
        static const int RoutineNotCompleteOrServiceInProgress = 0x23;
        static const int RequestSequenceError = 0x24;
        static const int RequestOutOfRange = 0x31;
        static const int SecurityAccessDenied = 0x33;
        static const int InvalidKey = 0x35;
        static const int ExceedNumberOfAttempts = 0x36;
        static const int RequiredTimeDelayHasNotExpired = 0x37;
        static const int ImproperDownloadType = 0x41;
        static const int CanNotDownloadToSpecifiedAddress = 0x42;
        static const int CanNotDownloadNumberOfBytesRequested = 0x43;
        static const int UploadNotAccepted = 0x50;
        static const int ImproperUploadType = 0x51;
        static const int CanNotUploadFromSpecifiedAddress = 0x52;
        static const int CanNotUploadNumberOfBytesRequested = 0x53;
        static const int TransferDataSuspended = 0x71;
        static const int TransferAborted = 0x72;
        static const int IllegalAddressInBlockTransfer = 0x74;
        static const int IllegalByteCountInBlockTransfer = 0x75;
        static const int IllegalBlockTransferType = 0x76;
        static const int BlockTransferDataChecksumError = 0x77;
        static const int BusyResponsePending = 0x78;
        static const int IncorrectByteCountDuringBlockTransfer = 0x79;
        static const int SubFunctionNotSupportedInActiveSession = 0x7E;
        static const int ServiceOrSubfunctionNotSupported = 0x7F;
        static const int ServiceNotSupportedInActiveSession = 0x80;
        static const int NoProgram = 0x90;
    };

    TP20Error(uint8_t errorCode);

    uint8_t getErrorCode() const noexcept;

private:
    uint8_t _errorCode;
};

} // namespace common
