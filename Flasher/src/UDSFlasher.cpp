#include "flasher/UDSFlasher.hpp"

#include <j2534/J2534.hpp>
#include <j2534/J2534Channel.hpp>

#include <common/CommonData.hpp>
#include <common/UDSMessage.hpp>
#include <common/UDSProtocolCommonSteps.hpp>
#include <common/Util.hpp>

#define HFSM2_ENABLE_ALL
#include <common/hfsm2/machine.hpp>

namespace flasher {

    struct AdditionalInfo {
        unsigned long baudrate;
        uint32_t canId;
    };

    AdditionalInfo getAdditionalInfo(common::CarPlatform carPlatform, uint32_t ecuId)
    {
        const auto configurationInfo{ common::loadConfiguration(common::CommonData::commonConfiguration) };
        const auto ecuInfo{ getEcuInfoByEcuId(configurationInfo, carPlatform, ecuId) };
        return { std::get<0>(ecuInfo).baudrate, std::get<1>(ecuInfo).canId };
    }

    class UDSFlasherImpl {
    public:
        UDSFlasherImpl(j2534::J2534& j2534, const UDSFlasherData& flasherData,
                       const std::function<void(FlasherState)>& stateUpdater)
            : _j2534{ j2534 }
            , _flasherData{ flasherData }
            , _additionalInfo{ getAdditionalInfo(_flasherData.carPlatform, _flasherData.ecuId) }
            , _isFailed{ false }
            , _stateUpdater{ stateUpdater }
            , _activeChannel{}
        {
        }

        void openChannels()
        {
            _stateUpdater(FlasherState::OpenChannels);
            _channels = common::UDSProtocolCommonSteps::openChannels(_j2534, _additionalInfo.baudrate,
                                                                     _additionalInfo.canId);
            if (_channels.empty()) {
                setFailed("Can't open channels");
            }
            else {
                const auto configurationInfo{ common::loadConfiguration(common::CommonData::commonConfiguration) };
                _activeChannel = &common::getChannelByEcuId(
                    configurationInfo, _flasherData.carPlatform, _flasherData.ecuId, _channels);
            }
        }

        void fallAsleep()
        {
            _stateUpdater(FlasherState::FallAsleep);
            if (!common::UDSProtocolCommonSteps::fallAsleep(_channels)) {
                setFailed("Fall asleep failed");
            }
        }

        void authorize()
        {
            _stateUpdater(FlasherState::Authorize);
            if (!common::UDSProtocolCommonSteps::authorize(*_activeChannel, _additionalInfo.canId, _flasherData.pin)) {
                setFailed("Authorization failed");
            }
        }

        void loadBootloader()
        {
            _stateUpdater(FlasherState::LoadBootloader);
            if (!common::UDSProtocolCommonSteps::transferData(*_activeChannel, _additionalInfo.canId, _flasherData.bootloader)) {
                setFailed("Bootloader loading failed");
            }
        }

        void startBootloader()
        {
            _stateUpdater(FlasherState::StartBootloader);
            if (!common::UDSProtocolCommonSteps::startRoutine(*_activeChannel, _additionalInfo.canId, _flasherData.bootloader.header.call)) {
                setFailed("Bootloader starting failed");
            }
        }

        void eraseFlash()
        {
            _stateUpdater(FlasherState::EraseFlash);
            if (!common::UDSProtocolCommonSteps::eraseFlash(*_activeChannel, _additionalInfo.canId, _flasherData.flash)) {
                setFailed("Flash erasing failed");
            }
        }

        void writeFlash()
        {
            _stateUpdater(FlasherState::WriteFlash);
            if (!common::UDSProtocolCommonSteps::transferData(*_activeChannel, _additionalInfo.canId, _flasherData.flash)) {
                setFailed("Flash writing failed");
            }
        }

        void wakeUp()
        {
            _stateUpdater(FlasherState::WakeUp);
            common::UDSProtocolCommonSteps::wakeUp(_channels);
        }

        void closeChannels()
        {
            _stateUpdater(FlasherState::CloseChannels);
            _activeChannel = nullptr;
            _channels.clear();
        }

        void done()
        {
            _stateUpdater(FlasherState::Done);
        }

        void error()
        {
            _stateUpdater(FlasherState::Error);
        }

        bool isFailed() const
        {
            return _isFailed;
        }

    private:
        void setFailed(const std::string& message)
        {
            _isFailed = true;
            _errorMessage = message;
        }

    private:
        j2534::J2534& _j2534;
        const UDSFlasherData _flasherData;
        const AdditionalInfo _additionalInfo;
        bool _isFailed;
        std::string _errorMessage;
        std::vector<std::unique_ptr<j2534::J2534Channel>> _channels;
        const std::function<void(FlasherState)> _stateUpdater;
        j2534::J2534Channel* _activeChannel;
    };

using M = hfsm2::MachineT<hfsm2::Config::ContextT<UDSFlasherImpl&>>;
    using FSM = M::PeerRoot<
        M::Composite<
            struct StartWork,
            struct OpenChannels,
            struct FallAsleep,
            struct Authorize,
            struct LoadBootloader,
            struct StartBootloader,
            struct EraseFlash,
            struct WriteFlash>,
        M::Composite<
            struct Finish,
            struct WakeUp,
            struct CloseChannels,
            struct Done,
            struct Error>
        >;

    struct BaseState : public FSM::State {
    public:
        void update(FullControl& control)
        {
            if (!control.context().isFailed()) {
                control.succeed();
            }
            else {
                control.fail();
            }
        }
    };

    struct BaseSuccesState : public FSM::State {
    public:
        void update(FullControl& control)
        {
            control.succeed();
        }
    };

    struct StartWork : public FSM::State {
        void enter(PlanControl& control)
        {
            auto plan = control.plan();
            plan.change<OpenChannels, FallAsleep>();
            plan.change<FallAsleep, Authorize>();
            plan.change<Authorize, LoadBootloader>();
            plan.change<LoadBootloader, StartBootloader>();
            plan.change<StartBootloader, EraseFlash>();
            plan.change<EraseFlash, WriteFlash>();
        }

        void planSucceeded(FullControl& control) {
            control.changeTo<Finish>();
        }

        void planFailed(FullControl& control)
        {
            control.changeTo<Finish>();
        }
    };

    struct OpenChannels : public BaseState {
        void enter(PlanControl& control)
        {
            control.context().openChannels();
        }
    };

    struct FallAsleep : public BaseState {
        void enter(PlanControl& control)
        {
            control.context().fallAsleep();
        }
    };

    struct Authorize : public BaseState {
        void enter(PlanControl& control)
        {
            control.context().authorize();
        }
    };

    struct LoadBootloader : public BaseState {
        void enter(PlanControl& control)
        {
            control.context().loadBootloader();
        }
    };

    struct StartBootloader : public BaseState {
        void enter(PlanControl& control)
        {
            control.context().startBootloader();
        }
    };

    struct EraseFlash : public BaseState {
        void enter(PlanControl& control)
        {
            control.context().eraseFlash();
        }
    };

    struct WriteFlash : public BaseState {
        void enter(PlanControl& control)
        {
            control.context().writeFlash();
        }
    };

    struct Finish : public FSM::State {
        void enter(PlanControl& control)
        {
            auto plan = control.plan();
            plan.change<WakeUp, CloseChannels>();
            if (control.context().isFailed()) {
                plan.change<CloseChannels, Error>();
            }
            else {
                plan.change<CloseChannels, Done>();
            }
        }
    };

    struct WakeUp : public BaseSuccesState {
        void enter(PlanControl& control)
        {
            control.context().wakeUp();
        }
    };

    struct CloseChannels : public BaseSuccesState {
        void enter(PlanControl& control)
        {
            control.context().closeChannels();
        }
    };

    struct Done : public BaseSuccesState {
        void enter(PlanControl& control)
        {
            control.context().done();
        }
    };

    struct Error : public BaseSuccesState {
        void enter(PlanControl& control)
        {
            control.context().error();
        }
    };

    common::CommonStepData createCommonStepData(common::CarPlatform carPlatform, uint32_t ecuId)
    {
        const auto configurationInfo{ common::loadConfiguration(common::CommonData::commonConfiguration) };
        const auto ecuInfo{ getEcuInfoByEcuId(configurationInfo, carPlatform, ecuId) };
        return { {}, configurationInfo, carPlatform, ecuId, std::get<1>(ecuInfo).canId, 0 };
    }

    UDSFlasher::UDSFlasher(j2534::J2534& j2534, const UDSFlasherData& flasherData)
        : FlasherBase{ j2534 }
        , _flasherData{ flasherData }
    {
    }

    UDSFlasher::~UDSFlasher()
    {
    }

    void UDSFlasher::startImpl()
    {
        UDSFlasherImpl impl(getJ2534(), _flasherData, [this](FlasherState state) {
            setCurrentState(state);
        });

        FSM::Instance fsm{ impl };

        while(getCurrentState() != FlasherState::Done || getCurrentState() != FlasherState::Error) {
            fsm.update();
        }
    }

} // namespace flasher
