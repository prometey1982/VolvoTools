#include "flasher/UDSFlasher.hpp"

#include <j2534/J2534.hpp>
#include <j2534/J2534Channel.hpp>

#include <common/CommonData.hpp>
#include <common/UDSMessage.hpp>
#include <common/UDSProtocolCommonSteps.hpp>
#include <common/Util.hpp>

#include <common/hfsm2/machine.hpp>

#include <optional>
#include <unordered_map>

namespace flasher {

    using M = hfsm2::MachineT<hfsm2::Config::ContextT<UDSFlasher&>>;
    using FSM = M::PeerRoot<
            struct Initial,
            struct OpeningChannels,
            struct FallingAsleep,
            struct KeepingAlive,
            struct Authorizing,
            struct BootloaderLoading,
            struct BooloaderStarting,
            struct FlashErasing,
            struct FlashWriting,
            struct WakingUp,
            struct ClosingChannels,
            struct Error,
            struct Done
        >;

    common::CommonStepData createCommonStepData(common::CarPlatform carPlatform, uint32_t ecuId)
    {
        const auto configurationInfo{ common::loadConfiguration(common::CommonData::commonConfiguration) };
        const auto ecuInfo{ getEcuInfoByEcuId(configurationInfo, carPlatform, ecuId) };
        return { {}, configurationInfo, carPlatform, ecuId, std::get<1>(ecuInfo).canId, 0 };
    }

    UDSFlasher::UDSFlasher(j2534::J2534& j2534, common::CommonStepData&& commonStepData, const std::array<uint8_t, 5>& pin,
        const common::VBF& bootloader, const common::VBF& flash)
        : UDSProtocolBase{ j2534, commonStepData.canId }
        , _pin{ pin }
        , _bootloader{ bootloader }
        , _flash{ flash }
        , _commonStepData{ std::move(commonStepData) }
    {
        registerStep(std::make_unique<common::OpenChannelsStep>(getJ2534(), _commonStepData));
        registerStep(std::make_unique<common::FallingAsleepStep>(_commonStepData));
        registerStep(std::make_unique<common::KeepAliveStep>(_commonStepData));
        registerStep(std::make_unique<common::AuthorizingStep>(_commonStepData, _pin));
        registerStep(std::make_unique<common::DataTransferStep>(common::UDSStepType::BootloaderLoading, _commonStepData, _bootloader));
        registerStep(std::make_unique<common::StartRoutineStep>(_commonStepData, _bootloader.header));
        registerStep(std::make_unique<common::FlashErasingStep>(_commonStepData, _flash));
        registerStep(std::make_unique<common::DataTransferStep>(common::UDSStepType::FlashLoading, _commonStepData, _flash));
        registerStep(std::make_unique<common::WakeUpStep>(_commonStepData));
        registerStep(std::make_unique<common::CloseChannelsStep>(_commonStepData));
    }

    UDSFlasher::~UDSFlasher()
    {
        _thread.join();
    }

    void UDSFlasher::start()
    {
        _thread = std::thread([this] {
            run();
            });
    }

} // namespace flasher
