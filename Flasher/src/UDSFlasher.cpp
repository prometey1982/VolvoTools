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

    class UDSFlasherImpl {
    public:
        UDSFlasherImpl(common::J2534Info& j2534Info, const FlasherParameters& flasherParameters,
                       const UDSFlasherParameters& udsFlasherParameters,
                       uint32_t canId,
                       const std::function<void(FlasherState)>& stateUpdater)
            : _j2534Info{ j2534Info }
            , _flasherParameters{ flasherParameters }
            , _udsFlasherParameters{ udsFlasherParameters }
            , _canId{ canId }
            , _isFailed{ false }
            , _stateUpdater{ stateUpdater }
            , _activeChannel{ _j2534Info.getChannelForEcu(_flasherParameters.ecuId) }
        {
        }

        void fallAsleep()
        {
            _stateUpdater(FlasherState::FallAsleep);
            if (!common::UDSProtocolCommonSteps::fallAsleep(_j2534Info.getChannels())) {
                setFailed("Fall asleep failed");
            }
        }

        void authorize()
        {
            _stateUpdater(FlasherState::Authorize);
            if (!common::UDSProtocolCommonSteps::authorize(_activeChannel, _canId, _udsFlasherParameters.pin)) {
                setFailed("Authorization failed");
            }
        }

        void loadBootloader()
        {
            _stateUpdater(FlasherState::LoadBootloader);
            if (_flasherParameters.sblProvider) {
                const auto bootloader{ _flasherParameters.sblProvider->getSBL(
                    _flasherParameters.carPlatform, _flasherParameters.ecuId, _flasherParameters.additionalData)};
                if (bootloader.chunks.empty() || !common::UDSProtocolCommonSteps::transferData(_activeChannel, _canId, bootloader)) {
                    setFailed("Bootloader loading failed");
                }
            }
        }

        void startBootloader()
        {
            _stateUpdater(FlasherState::StartBootloader);
            if (_flasherParameters.sblProvider) {
                const auto bootloader{ _flasherParameters.sblProvider->getSBL(
                    _flasherParameters.carPlatform, _flasherParameters.ecuId, _flasherParameters.additionalData) };
                if (!common::UDSProtocolCommonSteps::startRoutine(_activeChannel, _canId, bootloader.header.call)) {
                    setFailed("Bootloader starting failed");
                }
            }
        }

        void eraseFlash()
        {
            _stateUpdater(FlasherState::EraseFlash);
            if (!common::UDSProtocolCommonSteps::eraseFlash(_activeChannel, _canId, _udsFlasherParameters.flash)) {
                setFailed("Flash erasing failed");
            }
        }

        void writeFlash()
        {
            _stateUpdater(FlasherState::WriteFlash);
            if (!common::UDSProtocolCommonSteps::transferData(_activeChannel, _canId, _udsFlasherParameters.flash)) {
                setFailed("Flash writing failed");
            }
        }

        void wakeUp()
        {
            _stateUpdater(FlasherState::WakeUp);
            common::UDSProtocolCommonSteps::wakeUp(_j2534Info.getChannels());
        }

        void closeChannels()
        {
            _stateUpdater(FlasherState::CloseChannels);
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
        common::J2534Info& _j2534Info;
        const FlasherParameters& _flasherParameters;
        const UDSFlasherParameters& _udsFlasherParameters;
        const uint32_t _canId;
        bool _isFailed;
        std::string _errorMessage;
        const std::function<void(FlasherState)> _stateUpdater;
        const j2534::J2534Channel& _activeChannel;
    };

using M = hfsm2::MachineT<hfsm2::Config::ContextT<UDSFlasherImpl&>>;
    using FSM = M::PeerRoot<
        M::Composite<
            struct StartWork,
            struct FallAsleep,
            struct Authorize,
            struct LoadBootloader,
            struct StartBootloader,
            struct EraseFlash,
            struct WriteFlash>,
        M::Composite<
            struct Finish,
            struct WakeUp,
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
            if (control.context().isFailed()) {
                plan.change<WakeUp, Error>();
            }
            else {
                plan.change<WakeUp, Done>();
            }
        }
    };

    struct WakeUp : public BaseSuccesState {
        void enter(PlanControl& control)
        {
            control.context().wakeUp();
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

    UDSFlasher::UDSFlasher(common::J2534Info& j2534Info, FlasherParameters&& flasherParaneters, UDSFlasherParameters&& udsFlasherParameters)
        : FlasherBase{ j2534Info, std::move(flasherParaneters) }
        , _configurationInfo{ common::loadConfiguration(common::CommonData::commonConfiguration) }
        , _udsFlasherParameters{ std::move(udsFlasherParameters) }
    {
    }

    UDSFlasher::~UDSFlasher()
    {
    }

    void UDSFlasher::startImpl()
    {
        const auto ecuInfo{ common::getEcuInfoByEcuId(_configurationInfo, getFlasherParameters().carPlatform,
            getFlasherParameters().ecuId) };

        UDSFlasherImpl impl(getJ2534Info(), getFlasherParameters(), _udsFlasherParameters,
            std::get<1>(ecuInfo).canId, [this](FlasherState state) {
            setCurrentState(state);
        });

        FSM::Instance fsm{ impl };

        while(getCurrentState() != FlasherState::Done || getCurrentState() != FlasherState::Error) {
            fsm.update();
        }
    }

} // namespace flasher
