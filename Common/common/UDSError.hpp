#pragma once

#include <stdexcept>


namespace common {

class UDSError: public std::runtime_error {
public:
    struct ErrorCode {
        static const int GenericError = 0x10;
        static const int ServiceNotSupported = 0x11;
        static const int SubFunctionNotSupported = 0x12;
        static const int InvalidMessageOrLengthFormat = 0x13;
        static const int ResponseTooLong = 0x14;
        static const int BusyRepeatRequest = 0x21;
        static const int ConditionsNotCorrect = 0x22;
        static const int RequestSequenceError = 0x24;
        static const int NoResponseFromSubnetComponent = 0x25;
        static const int FailurePreventsExecutionOfRequestedAction = 0x26;
        static const int RequestOutOfRange = 0x31;
        static const int SecurityAccessDenied = 0x33;
        static const int InvalidKey = 0x35;
        static const int ExceedNumberOfAttempts = 0x36;
        static const int RequiredTimeDelayHasNotExpired = 0x37;
        static const int UploadDownloadNotAccepted = 0x70;
        static const int TransferDataSuspended = 0x71;
        static const int ProgrammingFailure = 0x72;
        static const int WrongBlockSequenceCounter = 0x73;
        static const int RequestReceivedResponsePending = 0x78;
        static const int SubFunctionNotSupportedInActiveSession = 0x7E;
        static const int ServiceNotSupportedInActiveSession = 0x7F;
        static const int RPMTooHigh = 0x81;
        static const int RPMTooLow = 0x82;
        static const int EngineIsRunning = 0x83;
        static const int EngineIsNotRunning = 0x84;
        static const int EngineRunTimeTooLow = 0x85;
        static const int TemperatureTooHigh = 0x86;
        static const int TemperatureTooLow = 0x87;
        static const int SpeedTooHigh = 0x88;
        static const int SpeedTooLow = 0x89;
        static const int ThrottlePedalTooHigh = 0x8A;
        static const int ThrottlePedalTooLow = 0x8B;
        static const int TransmissionRangeNotInNeutral = 0x8C;
        static const int TransmissionRangeNotInGear = 0x8D;
        static const int BrakeSwitchesNotClosed = 0x8F;
        static const int ShifterLevelNotInPark = 0x90;
        static const int TorqueConverterClutchLocked = 0x91;
        static const int VoltageTooHigh = 0x92;
        static const int VoltageTooLow = 0x93;
    };

    UDSError(uint8_t errorCode);

    uint8_t getErrorCode() const noexcept;

private:
    uint8_t _errorCode;
};

} // namespace common
