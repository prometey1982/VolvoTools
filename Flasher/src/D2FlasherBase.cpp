#include "flasher/D2FlasherBase.hpp"

#include "common/ICanChannel.hpp"
#include <common/Util.hpp>
#include <common/protocols/D2ProtocolCommonSteps.hpp>
#include <common/SBL.hpp>
#include <common/VBFParser.hpp>
#include <j2534/J2534.hpp>
#include <j2534/J2534Channel.hpp>

#include <ctime>
#include <numeric>

namespace flasher {

D2FlasherBase::D2FlasherBase(j2534::J2534 &j2534, FlasherParameters&& flasherParameters)
    : FlasherBase{ j2534, std::move(flasherParameters) }
{
}

D2FlasherBase::~D2FlasherBase()
{
}

void D2FlasherBase::canWakeUp(unsigned long baudrate)
{
    canWakeUp();
    cleanErrors();
}

void D2FlasherBase::startImpl(std::vector<std::unique_ptr<ICanChannel>>& channels)
{
    try {
        const size_t simpleStepSize = 100;
        const common::CarPlatform carPlatform{ getFlasherParameters().carPlatform };
        const auto ecuId{ getFlasherParameters().ecuId };
        const auto additionalData{ getFlasherParameters().additionalData };
        const auto bootloader = getFlasherParameters().sblProvider->getSBL(carPlatform, ecuId, additionalData);
        if (isBootloaderRequired() && bootloader.chunks.empty()) {
            throw std::runtime_error("Secondary bootloader not found");
        }
        std::this_thread::sleep_for(std::chrono::seconds(1));
        common::D2ProtocolCommonSteps::wakeUp(channels);
        setCurrentProgress(0);
        setMaximumProgress(simpleStepSize * 4 + getMaximumFlashProgress() + getProgressFromVBF(bootloader));
        setCurrentState(FlasherState::FallAsleep);
        common::D2ProtocolCommonSteps::fallAsleep(channels);
        auto& channel{ common::getChannelByEcuId(carPlatform, ecuId, channels) };
        common::D2ProtocolCommonSteps::startPBL(channel, ecuId);
        if (!bootloader.chunks.empty()) {
            setCurrentState(FlasherState::LoadBootloader);
            common::D2ProtocolCommonSteps::transferData(channel, ecuId, bootloader, [this](size_t progress) {
                incCurrentProgress(progress);
                });
            setCurrentState(FlasherState::StartBootloader);
            common::D2ProtocolCommonSteps::startRoutine(channel, ecuId, bootloader.header.call);
        }
        setCurrentState(FlasherState::EraseFlash);
        eraseStep(channel, ecuId);
        setCurrentState(FlasherState::WriteFlash);
        writeStep(channel, ecuId);
        setCurrentState(FlasherState::WakeUp);
        common::D2ProtocolCommonSteps::wakeUp(channels);
        std::this_thread::sleep_for(std::chrono::seconds(2));
        common::D2ProtocolCommonSteps::setDIMTime(channels);
        setCurrentState(FlasherState::Done);
    }
    catch(...) {
        setCurrentState(FlasherState::WakeUp);
        common::D2ProtocolCommonSteps::wakeUp(channels);
        std::this_thread::sleep_for(std::chrono::seconds(2));
        common::D2ProtocolCommonSteps::setDIMTime(channels);
        setCurrentState(FlasherState::Error);
    }
}


void D2FlasherBase::canWakeUp() {
  setCurrentState(FlasherState::WakeUp);
}

void D2FlasherBase::cleanErrors() {
}

} // namespace flasher
