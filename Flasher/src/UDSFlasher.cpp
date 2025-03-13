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
        UDSFlasherImpl(const std::vector<std::unique_ptr<j2534::J2534Channel>>& channels,
                       const FlasherParameters& flasherParameters,
                       const UDSFlasherParameters& udsFlasherParameters,
                       uint32_t canId,
                       const std::function<void(FlasherState)>& stateUpdater,
                       const std::function<void(size_t)>& progressUpdater)
            : _channels{ channels }
            , _flasherParameters{ flasherParameters }
            , _udsFlasherParameters{ udsFlasherParameters }
            , _canId{ canId }
            , _isFailed{ false }
            , _stateUpdater{ stateUpdater }
            , _progressUpdater{ progressUpdater }
        {
        }

        size_t getMaximumProgress()
        {
            const auto bootloader{ _flasherParameters.sblProvider->getSBL(
                _flasherParameters.carPlatform, _flasherParameters.ecuId, _flasherParameters.additionalData)};
            return FlasherBase::getProgressFromVBF(bootloader) + FlasherBase::getProgressFromVBF(_flasherParameters.flash);
        }

        void fallAsleep()
        {
            _stateUpdater(FlasherState::FallAsleep);
            if (!common::UDSProtocolCommonSteps::fallAsleep(_channels)) {
                setFailed("Fall asleep failed");
            }
        }

        void keepAlive()
        {
            auto& channel{ common::getChannelByEcuId(_flasherParameters.carPlatform, _flasherParameters.ecuId, _channels) };
            common::UDSProtocolCommonSteps::keepAlive(channel);
        }

        void authorize()
        {
            _stateUpdater(FlasherState::Authorize);
            auto& channel{ common::getChannelByEcuId(_flasherParameters.carPlatform, _flasherParameters.ecuId, _channels) };
            if (!common::UDSProtocolCommonSteps::authorize(channel, _canId, _udsFlasherParameters.pin)) {
                setFailed("Authorization failed");
            }
        }

        void loadBootloader()
        {
            _stateUpdater(FlasherState::LoadBootloader);
            if (_flasherParameters.sblProvider) {
                const auto bootloader{ _flasherParameters.sblProvider->getSBL(
                    _flasherParameters.carPlatform, _flasherParameters.ecuId, _flasherParameters.additionalData)};
                auto& channel{ common::getChannelByEcuId(_flasherParameters.carPlatform, _flasherParameters.ecuId, _channels) };
                if (bootloader.chunks.empty() || !common::UDSProtocolCommonSteps::transferData(channel, _canId, bootloader,
                                                                                               _progressUpdater)) {
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
                auto& channel{ common::getChannelByEcuId(_flasherParameters.carPlatform, _flasherParameters.ecuId, _channels) };
                if (!common::UDSProtocolCommonSteps::startRoutine(channel, _canId, bootloader.header.call)) {
                    setFailed("Bootloader starting failed");
                }
            }
        }

        void eraseFlash()
        {
            _stateUpdater(FlasherState::EraseFlash);
            auto& channel{ common::getChannelByEcuId(_flasherParameters.carPlatform, _flasherParameters.ecuId, _channels) };
            if (!common::UDSProtocolCommonSteps::eraseFlash(channel, _canId, _flasherParameters.flash)) {
                setFailed("Flash erasing failed");
            }
        }

        void writeFlash()
        {
            _stateUpdater(FlasherState::WriteFlash);
            auto& channel{ common::getChannelByEcuId(_flasherParameters.carPlatform, _flasherParameters.ecuId, _channels) };
            if (!common::UDSProtocolCommonSteps::transferData(channel, _canId, _flasherParameters.flash,
                                                              _progressUpdater)) {
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
        const std::vector<std::unique_ptr<j2534::J2534Channel>>& _channels;
        const FlasherParameters& _flasherParameters;
        const UDSFlasherParameters& _udsFlasherParameters;
        const uint32_t _canId;
        bool _isFailed;
        std::string _errorMessage;
        const std::function<void(FlasherState)> _stateUpdater;
        const std::function<void(size_t)> _progressUpdater;
    };

using M = hfsm2::MachineT<hfsm2::Config::ContextT<UDSFlasherImpl&>>;
    using FSM = M::PeerRoot<
        M::Composite<
            struct StartWork,
            struct FallAsleep,
            struct KeepAlive,
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
            plan.change<FallAsleep, KeepAlive>();
            plan.change<KeepAlive, Authorize>();
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

    struct KeepAlive : public BaseState {
        void enter(PlanControl& control)
        {
            control.context().keepAlive();
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

    UDSFlasher::UDSFlasher(j2534::J2534& j2534, FlasherParameters&& flasherParameters, UDSFlasherParameters&& udsFlasherParameters)
        : FlasherBase{ j2534, std::move(flasherParameters) }
        , _udsFlasherParameters{ std::move(udsFlasherParameters) }
    {
    }

    UDSFlasher::~UDSFlasher()
    {
    }

    void UDSFlasher::startImpl(std::vector<std::unique_ptr<j2534::J2534Channel>>& channels)
    {
        const auto ecuInfo{ common::getEcuInfoByEcuId(getFlasherParameters().carPlatform,
            getFlasherParameters().ecuId) };

        UDSFlasherImpl impl(channels, getFlasherParameters(), _udsFlasherParameters,
            std::get<1>(ecuInfo).canId, [this](FlasherState state) {
            setCurrentState(state);
        },
            [this](size_t progress) {
                incCurrentProgress(progress);
            });

        setMaximumProgress(impl.getMaximumProgress());

        FSM::Instance fsm{ impl };

        while(getCurrentState() != FlasherState::Done && getCurrentState() != FlasherState::Error) {
            fsm.update();
        }
    }

} // namespace flasher
