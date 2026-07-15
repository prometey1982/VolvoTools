#include "flasher/UDSFlasher.hpp"

#include <j2534/J2534.hpp>
#include <j2534/J2534Channel.hpp>

#include <common/CommonData.hpp>
#include <common/CanIdProvider.hpp>
#include <common/protocols/UDSMessage.hpp>
#include <common/protocols/UDSProtocolCommonSteps.hpp>

#define LOG_MODULE_NAME "flasher"
#include <common/LogHelper.hpp>
#include <common/ICanChannel.hpp>
#include <common/Util.hpp>

#define HFSM2_ENABLE_ALL
#include <common/hfsm2/machine.hpp>

namespace flasher {

    class UDSFlasherImpl {
    public:
        UDSFlasherImpl(const std::vector<std::unique_ptr<common::ICanChannel>>& channels,
                       common::CarPlatform carPlatform,
                       uint32_t ecuId,
                       const UDSFlasherConfig& config,
                       std::unique_ptr<common::CanIdProvider> canIdProvider,
                       const std::function<void(FlasherState)>& stateUpdater,
                       const std::function<void(size_t)>& progressUpdater)
            : _channels{ channels }
            , _carPlatform{ carPlatform }
            , _ecuId{ ecuId }
            , _config{ config }
            , _canIdProvider{ std::move(canIdProvider) }
            , _isFailed{ false }
            , _stateUpdater{ stateUpdater }
            , _progressUpdater{ progressUpdater }
        {
        }

        size_t getMaximumProgress()
        {
            return FlasherBase::getProgressFromVBF(_config.bootloader) + FlasherBase::getProgressFromVBF(_config.flash);
        }

        void fallAsleep()
        {
            _stateUpdater(FlasherState::FallAsleep);
            if (!common::UDSProtocolCommonSteps::fallAsleep(_channels, _canIdProvider->getFuncCanId())) {
                setFailed("Fall asleep failed");
            }
        }

        void keepAlive()
        {
            auto& channel{ common::getChannelByEcuId(_carPlatform, _ecuId, _channels) };
            common::UDSProtocolCommonSteps::keepAlive(channel, _canIdProvider->getFuncCanId());
        }

        void authorize()
        {
            _stateUpdater(FlasherState::Authorize);
            auto& channel{ common::getChannelByEcuId(_carPlatform, _ecuId, _channels) };
            if (!common::UDSProtocolCommonSteps::authorize(channel, _canIdProvider->getPhysCanId(), _config.pin)) {
                setFailed("Authorization failed");
            }
        }

        void loadBootloader()
        {
            _stateUpdater(FlasherState::LoadBootloader);
            if (!_config.bootloader.chunks.empty()) {
                auto& channel{ common::getChannelByEcuId(_carPlatform, _ecuId, _channels) };
                if (!common::UDSProtocolCommonSteps::transferData(channel, _canIdProvider->getPhysCanId(), _config.bootloader,
                                                                                                _progressUpdater)) {
                    setFailed("Bootloader loading failed");
                }
            }
        }

        void startBootloader()
        {
            _stateUpdater(FlasherState::StartBootloader);
            if (!_config.bootloader.chunks.empty()) {
                auto& channel{ common::getChannelByEcuId(_carPlatform, _ecuId, _channels) };
                if (!common::UDSProtocolCommonSteps::startRoutine(channel, _canIdProvider->getPhysCanId(), _config.bootloader.header.call)) {
                    setFailed("Bootloader starting failed");
                }
            }
        }

        void writeFlash()
        {
            auto& channel{ common::getChannelByEcuId(_carPlatform, _ecuId, _channels) };
            for(const auto& chunk: _config.flash.chunks) {
                _stateUpdater(FlasherState::EraseFlash);
                if (!common::UDSProtocolCommonSteps::eraseChunk(channel, _canIdProvider->getPhysCanId(), chunk)) {
                    setFailed("Flash erasing failed");
                }
                _stateUpdater(FlasherState::WriteFlash);
                if (!common::UDSProtocolCommonSteps::transferChunk(channel, _canIdProvider->getPhysCanId(), chunk,
                                                                    _progressUpdater)) {
                    setFailed("Flash writing failed");
                }
            }
        }

        void checkValidApplication()
        {
            auto& channel{ common::getChannelByEcuId(_carPlatform, _ecuId, _channels) };
            common::UDSProtocolCommonSteps::checkValidApplication(channel, _canIdProvider->getPhysCanId());
        }

        void wakeUp()
        {
            _stateUpdater(FlasherState::WakeUp);
            common::UDSProtocolCommonSteps::wakeUp(_channels, _canIdProvider->getFuncCanId());
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
            LOG_MODULE(ERROR) << message;
            _isFailed = true;
            _errorMessage = message;
        }

    private:
        const std::vector<std::unique_ptr<common::ICanChannel>>& _channels;
        common::CarPlatform _carPlatform;
        uint32_t _ecuId;
        const UDSFlasherConfig& _config;
        std::unique_ptr<common::CanIdProvider> _canIdProvider;
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
            struct WriteFlash,
            struct CheckValidApplication>,
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
            plan.change<StartBootloader, WriteFlash>();
            plan.change<WriteFlash, CheckValidApplication>();
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

    struct WriteFlash : public BaseState {
        void enter(PlanControl& control)
        {
            control.context().writeFlash();
        }
    };

    struct CheckValidApplication : public BaseState {
        void enter(PlanControl& control)
        {
            control.context().checkValidApplication();
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

    UDSFlasher::UDSFlasher(j2534::J2534& j2534, common::CarPlatform carPlatform, uint32_t ecuId,
                           UDSFlasherConfig&& config)
        : FlasherBase{ j2534, carPlatform, ecuId }
        , _config{ std::move(config) }
    {
    }

    UDSFlasher::~UDSFlasher()
    {
    }

    void UDSFlasher::startImpl(std::vector<std::unique_ptr<common::ICanChannel>>& channels)
    {
        const auto ecuInfo{ common::getEcuInfoByEcuId(_carPlatform, _ecuId) };

        UDSFlasherImpl impl(channels, _carPlatform, _ecuId, _config,
            common::createCanIdProviderForEcu(_carPlatform, _ecuId), [this](FlasherState state) {
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
