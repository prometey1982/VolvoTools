#include "flasher/UDSFlasher.hpp"

#include <j2534/J2534.hpp>
#include <j2534/J2534Channel.hpp>

#include <common/CommonData.hpp>
#include <common/UDSMessage.hpp>
#include <common/UDSProtocolCommonSteps.hpp>
#include <common/Util.hpp>

#define HFSM2_ENABLE_ALL
#include <common/hfsm2/machine.hpp>

#include <optional>
#include <unordered_map>

namespace flasher {

    class UDSFlasherImpl: public common::UDSProtocolBase {
    public:
        UDSFlasherImpl(j2534::J2534& j2534, common::CommonStepData&& commonStepData, const std::array<uint8_t, 5>& pin,
                    const common::VBF& bootloader, const common::VBF& flash)
            : common::UDSProtocolBase{ j2534, commonStepData.canId }
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

    private:
        std::array<uint8_t, 5> _pin;
        common::VBF _bootloader;
        common::VBF _flash;
        common::CommonStepData _commonStepData;
        std::vector<FlasherCallback*> _callbacks;
    };

using M = hfsm2::MachineT<hfsm2::Config::ContextT<UDSFlasherImpl&>>;
    using FSM = M::PeerRoot<
        struct Initial,
        M::Composite<
            struct StartWork,
            struct OpenChannels,
            struct FallAsleep,
            struct Authorize,
            struct LoadBootloader,
            struct StartBootloader,
            struct EraseFlash,
            struct WriteFlash>,
        struct Error,
        M::Composite<
            struct Finish,
            struct WakeUp,
            struct CloseChannels,
            struct Done
            >
        >;

    common::CommonStepData createCommonStepData(common::CarPlatform carPlatform, uint32_t ecuId)
    {
        const auto configurationInfo{ common::loadConfiguration(common::CommonData::commonConfiguration) };
        const auto ecuInfo{ getEcuInfoByEcuId(configurationInfo, carPlatform, ecuId) };
        return { {}, configurationInfo, carPlatform, ecuId, std::get<1>(ecuInfo).canId, 0 };
    }

    UDSFlasher::UDSFlasher(j2534::J2534& j2534, common::CommonStepData&& commonStepData, const std::array<uint8_t, 5>& pin,
        const common::VBF& bootloader, const common::VBF& flash)
        : FlasherBase{ j2534 }
        , _impl{ std::make_unique<UDSFlasherImpl>(j2534, std::move(commonStepData), pin, bootloader, flash) }
    {
    }

    UDSFlasher::~UDSFlasher()
    {
    }

    void UDSFlasher::startImpl()
    {
        _impl->run();
    }

} // namespace flasher
