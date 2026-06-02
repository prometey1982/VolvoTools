#include "flasher/UDSReader.hpp"

#include <common/CommonData.hpp>
#include <common/Util.hpp>
#include <common/protocols/UDSProtocolCommonSteps.hpp>
#include <common/protocols/UDSRequest.hpp>

#include <easylogging++.h>

#include <functional>
#include <memory>
#include <stdexcept>
#include <tuple>
#include <utility>

namespace flasher {

namespace {

void setFailure(const std::string& message, const std::function<void(const std::string&)>& errorUpdater)
{
    LOG(ERROR) << message;
    errorUpdater(message);
    throw std::runtime_error(message);
}

}

UDSReader::UDSReader(j2534::J2534& j2534, FlasherParameters&& flasherParameters,
                     UDSReaderParameters&& udsReaderParameters, std::vector<uint8_t>& output)
    : FlasherBase{ j2534, std::move(flasherParameters) }
    , _udsReaderParameters{ std::move(udsReaderParameters) }
    , _output{ output }
{
}

UDSReader::~UDSReader()
{
}

std::vector<std::unique_ptr<j2534::J2534Channel>> UDSReader::openChannels()
{
    LOG(INFO) << "UDSReader opening target ECU channel only, ecu=0x" << std::hex
        << getFlasherParameters().ecuId;
    std::vector<std::unique_ptr<j2534::J2534Channel>> channels;
    auto channel = openChannelForEcu(getFlasherParameters().ecuId);
    if (!channel) {
        throw std::runtime_error("Failed to open UDS target ECU channel");
    }
    channels.emplace_back(std::move(channel));
    LOG(INFO) << "UDSReader target ECU channel opened";
    return channels;
}

void UDSReader::startImpl(std::vector<std::unique_ptr<j2534::J2534Channel>>& channels)
{
    LOG(INFO) << "UDSReader startImpl enter, channels=" << channels.size()
              << " start=0x" << std::hex << _udsReaderParameters.startAddress
              << " size=0x" << _udsReaderParameters.dataSize;
    const auto ecuInfo{ common::getEcuInfoByEcuId(getFlasherParameters().carPlatform,
        getFlasherParameters().ecuId) };
    const uint32_t canId = std::get<1>(ecuInfo).canId;
    auto errorUpdater = [this](const std::string& error) {
        setLastError(error);
    };

    try {
        std::unique_ptr<common::VBF> bootloader;
        if (!_udsReaderParameters.attachRunningSbl) {
            bootloader = std::make_unique<common::VBF>(getFlasherParameters().sblProvider->getSBL(
                getFlasherParameters().carPlatform, getFlasherParameters().ecuId, getFlasherParameters().additionalData));
        }
        setMaximumProgress((_udsReaderParameters.attachRunningSbl ? 0u : FlasherBase::getProgressFromVBF(*bootloader))
            + _udsReaderParameters.dataSize);

        if (_udsReaderParameters.attachRunningSbl) {
            setCurrentState(FlasherState::FallAsleep);
            LOG(INFO) << "Attaching to already running UDS SBL, skipping fallAsleep/authorize/load/start";
        }
        else if (_udsReaderParameters.skipFallAsleep) {
            setCurrentState(FlasherState::FallAsleep);
            LOG(INFO) << "Fall asleep skipped, vehicle programming mode was prepared by CEM";
        }
        else {
            setCurrentState(FlasherState::FallAsleep);
            if (!common::UDSProtocolCommonSteps::fallAsleep(channels)) {
                setFailure("Fall asleep failed", errorUpdater);
            }
        }

        auto& channel{ common::getChannelByEcuId(getFlasherParameters().carPlatform,
            getFlasherParameters().ecuId, channels) };

        if (_udsReaderParameters.attachRunningSbl) {
            setCurrentState(FlasherState::Authorize);
            if (!common::UDSProtocolCommonSteps::authorize(channel, canId, _udsReaderParameters.pin)) {
                setFailure("Running SBL authorization failed", errorUpdater);
            }
        }
        else {
            common::UDSProtocolCommonSteps::keepAlive(channel);

            setCurrentState(FlasherState::Authorize);
            try {
                common::UDSRequest programmingSession{ canId, { 0x10, 0x02 } };
                programmingSession.process(channel, { 0x02 }, 1, 1000);
            }
            catch (const std::exception& ex) {
                LOG(WARNING) << "Programming session request before authorize failed: " << ex.what();
            }
            if (!common::UDSProtocolCommonSteps::authorize(channel, canId, _udsReaderParameters.pin)) {
                setFailure("Authorization failed", errorUpdater);
            }

            setCurrentState(FlasherState::LoadBootloader);
            if (bootloader->chunks.empty() || !common::UDSProtocolCommonSteps::transferData(channel, canId, *bootloader,
                                                                                           [this](size_t progress) {
                                                                                               incCurrentProgress(progress);
                                                                                           })) {
                setFailure("Bootloader loading failed", errorUpdater);
            }

            setCurrentState(FlasherState::StartBootloader);
            if (!common::UDSProtocolCommonSteps::startRoutine(channel, canId, bootloader->header.call)) {
                setFailure("Bootloader starting failed", errorUpdater);
            }
        }

        setCurrentState(FlasherState::ReadFlash);
        if (!common::UDSProtocolCommonSteps::readDataByUpload(channel, canId,
                                                              _udsReaderParameters.startAddress,
                                                              _udsReaderParameters.dataSize,
                                                              _output,
                                                              [this](size_t progress) {
                                                                  incCurrentProgress(progress);
                                                              })) {
            setFailure("Flash reading failed", errorUpdater);
        }

        setCurrentState(FlasherState::WakeUp);
        common::UDSProtocolCommonSteps::wakeUp(channels);
        setCurrentState(FlasherState::Done);
    }
    catch (const std::exception& ex) {
        if (getLastError().empty()) {
            setLastError(ex.what());
        }
        LOG(WARNING) << "UDSReader failed after SBL workflow";
        if (getCurrentState() != FlasherState::WakeUp) {
            setCurrentState(FlasherState::WakeUp);
            common::UDSProtocolCommonSteps::wakeUp(channels);
        }
        setCurrentState(FlasherState::Error);
        throw;
    }
    catch (...) {
        if (getLastError().empty()) {
            setLastError("Unknown UDS reading error");
        }
        LOG(WARNING) << "UDSReader failed after SBL workflow";
        if (getCurrentState() != FlasherState::WakeUp) {
            setCurrentState(FlasherState::WakeUp);
            common::UDSProtocolCommonSteps::wakeUp(channels);
        }
        setCurrentState(FlasherState::Error);
        throw;
    }
}

} // namespace flasher
