#include "flasher/D2FlasherBase.hpp"

#include <common/Util.hpp>
#include <common/protocols/D2Message.hpp>
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

void D2FlasherBase::startImpl(std::vector<std::unique_ptr<j2534::J2534Channel>>& channels)
{
    try {
        const size_t simpleStepSize = 100;
        const common::CarPlatform carPlatform{ getFlasherParameters().carPlatform };
        const auto ecuId{ getFlasherParameters().ecuId };
        const auto additionalData{ getFlasherParameters().additionalData };
        const auto bootloader = getFlasherParameters().sblProvider->getSBL(carPlatform, ecuId, additionalData);
        if (bootloader.chunks.empty()) {
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
        setCurrentState(FlasherState::LoadBootloader);
        common::D2ProtocolCommonSteps::transferData(channel, ecuId, bootloader, [this](size_t progress) {
            incCurrentProgress(progress);
        });
        setCurrentState(FlasherState::StartBootloader);
        common::D2ProtocolCommonSteps::startRoutine(channel, ecuId, bootloader.header.call);
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
  // unsigned long numMsgs = 1;
  // const auto& channels{ getChannels() };
  // for (size_t i = 0; i < channels.size(); ++i) {
  //     if (channels[i]->getProtocolId() != ISO9141) {
  //         channels[i]->writeMsgs(common::D2Messages::wakeUpCanRequest, numMsgs, 5000);
  //     }
  // }

  // std::this_thread::sleep_for(std::chrono::seconds(2));

  // const auto now{std::chrono::system_clock::now()};
  // const auto time_t = std::chrono::system_clock::to_time_t(now);
  // struct tm lt;
  // localtime_s(&lt, &time_t);

  // auto& channel{ common::getChannelByEcuId(getFlasherParameters().carPlatform, getFlasherParameters().ecuId, getChannels()) };
  // channel.writeMsgs(
  //       common::D2Messages::setCurrentTime(lt.tm_hour, lt.tm_min), numMsgs,
  //       5000);
}

void D2FlasherBase::cleanErrors() {
  // for (const auto ecuId :
  //      {static_cast<uint8_t>(common::ECUType::ECM_ME), static_cast<uint8_t>(common::ECUType::TCM), static_cast<uint8_t>(common::ECUType::SRS)}) {
  //   unsigned long numMsgs = 1;
  //   auto& channel{ common::getChannelByEcuId(getFlasherParameters().carPlatform, ecuId, getChannels()) };
  //   channel.writeMsgs(common::D2Messages::clearDTCMsgs(ecuId), numMsgs);
  // }
}

} // namespace flasher
