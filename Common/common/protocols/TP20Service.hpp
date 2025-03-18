#pragma once

#include <cinttypes>

namespace common {

    struct KWPServiceID {
        //00-0F are SAE1979 requests
        //40-4F are SAE1979 responses to requests

        static const uint8_t SAEJ1979_ShowCurrentData = 0x01;
        static const uint8_t SAEJ1979_ShowFreezeFrameData = 0x02;
        static const uint8_t SAEJ1979_ShowDiagnosticTroubleCodes = 0x03;
        static const uint8_t SAEJ1979_ClearTroubleCodesAndStoredValues = 0x04;
        static const uint8_t SAEJ1979_TestResultOxygenSenors = 0x05;
        static const uint8_t SAEJ1979_TestResultsNonContinuouslyMonitored = 0x06;
        static const uint8_t SAEJ1979_ShowPendingTroubleCodes = 0x07;
        static const uint8_t SAEJ1979_SpecialControlMode = 0x08;
        static const uint8_t SAEJ1979_RequestVehicleInformation = 0x09;
        static const uint8_t SAEJ1979_RequestPermanentTroubleCodes = 0x0A;
        static const uint8_t StartDiagnosticSession = 0x10;
        static const uint8_t EcuReset = 0x11;
        static const uint8_t ReadFreezeFrameData = 0x12;
        static const uint8_t ReadDiagnosticTroubleCodes = 0x13;//KWP2000 spec forbids this
        static const uint8_t ClearDiagnosticInformation = 0x14;
        static const uint8_t ReadStatusOfDiagnosticTroubleCodes = 0x17;
        static const uint8_t ReadDiagnosticTroubleCodesByStatus = 0x18;
        static const uint8_t ReadECUIdentification = 0x1A;
        static const uint8_t StopDiagnosticSession = 0x20;//KWP2000 spec forbids this
        static const uint8_t ReadDataByLocalIdentifier = 0x21;
        static const uint8_t ReadDataByCommonIdentifier = 0x22;
        static const uint8_t ReadMemoryByAddress = 0x23;
        static const uint8_t StopRepeatedDataTransmission = 0x25;
        static const uint8_t SetDataRates = 0x26;
        static const uint8_t SecurityAccess = 0x27;
        static const uint8_t DynamicallyDefineLocalIdentifier = 0x2C;
        static const uint8_t WriteDataByCommonIdentifier = 0x2E;
        static const uint8_t InputOutputControlByCommonIdentifier = 0x2F;
        static const uint8_t InputOutputControlByLocalIdentifier = 0x30;
        static const uint8_t StartRoutineByLocalIdentifier = 0x31;
        static const uint8_t StopRoutineByLocalIdentifier = 0x32;
        static const uint8_t RequestRoutineResultsByLocalIdentifier = 0x33;
        static const uint8_t RequestDownload = 0x34;
        static const uint8_t RequestUpload = 0x35;
        static const uint8_t TransferData = 0x36;
        static const uint8_t RequestTransferExit = 0x37;
        static const uint8_t StartRoutineByAddress = 0x38;
        static const uint8_t StopRoutineByAddress = 0x39;
        static const uint8_t RequestRoutineResultsByAddress = 0x3A;
        static const uint8_t WriteDataByLocalIdentifier = 0x3B;
        static const uint8_t WriteMemoryByAddress = 0x3D;
        static const uint8_t TesterPresent = 0x3E;
        static const uint8_t StartDiagnosticSessionPositiveResponse = 0x50;
        static const uint8_t ReadDiagnosticTroubleCodesPositiveResponse = 0x53;
        static const uint8_t ClearDiagnosticInformationPositiveResponse = 0x54;
        static const uint8_t ReadStatusOfDiagnosticTroubleCodesPositiveResponse = 0x57;
        static const uint8_t ReadDiagnosticTroubleCodesByStatusPositiveResponse = 0x58;
        static const uint8_t ReadECUIdentificationPositiveResponse = 0x5A;
        static const uint8_t StopDiagnosticSessionPositiveResponse = 0x60;
        static const uint8_t ReadDataByLocalIdentifierPositiveResponse = 0x61;
        static const uint8_t ReadMemoryByAddressPositiveResponse = 0x63;
        static const uint8_t SecurityAccessPositiveResponse = 0x67;
        static const uint8_t StartRoutineByLocalIdentifierPositiveResponse = 0x71;
        static const uint8_t RequestRoutineResultsByLocalIdentifierPositiveResponse = 0x73;
        static const uint8_t RequestDownloadPositiveResponse = 0x74;
        static const uint8_t RequestUploadPositiveResponse = 0x75;
        static const uint8_t TransferDataPositiveResponse = 0x76;
        static const uint8_t RequestTransferExitPositiveResponse = 0x77;
        static const uint8_t WriteMemoryByAddressPositiveResponse = 0x7D;
        static const uint8_t TesterPresentPositiveReponse = 0x7E;
        static const uint8_t NegativeResponse = 0x7F;
        static const uint8_t EscapeCode = 0x80;
        static const uint8_t StartCommunication = 0x81;
        static const uint8_t StopCommunication = 0x82;
        static const uint8_t AccessTimingParameters = 0x83;
        static const uint8_t StartSession = 0xC0;
        static const uint8_t StartCommunicationPositiveResponse = 0xC1;
        static const uint8_t StopCommunicationPositiveResponse = 0xC2;
        static const uint8_t AccessTimingParametersPositiveResponse = 0xC3;
        static const uint8_t StartSessionPositiveResponse = 0xD0;
    };

} // namespace common
