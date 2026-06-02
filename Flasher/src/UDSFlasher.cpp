#include "flasher/UDSFlasher.hpp"

#include <j2534/J2534.hpp>
#include <j2534/J2534Channel.hpp>

#include <common/CommonData.hpp>
#include <common/protocols/UDSProtocolCommonSteps.hpp>
#include <common/protocols/UDSRequest.hpp>
#include <common/Util.hpp>

#include <easylogging++.h>

#include <stdexcept>

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
                       const std::function<void(size_t)>& progressUpdater,
                       const std::function<void(const std::string&)>& errorUpdater)
            : _channels{ channels }
            , _flasherParameters{ flasherParameters }
            , _udsFlasherParameters{ udsFlasherParameters }
            , _canId{ canId }
            , _isFailed{ false }
            , _stateUpdater{ stateUpdater }
            , _progressUpdater{ progressUpdater }
            , _errorUpdater{ errorUpdater }
        {
        }

        size_t getMaximumProgress()
        {
            size_t progress = FlasherBase::getProgressFromVBF(_flasherParameters.flash);
            if (!_udsFlasherParameters.attachRunningSbl && _flasherParameters.sblProvider) {
                const auto bootloader{ _flasherParameters.sblProvider->getSBL(
                    _flasherParameters.carPlatform, _flasherParameters.ecuId, _flasherParameters.additionalData)};
                progress += FlasherBase::getProgressFromVBF(bootloader);
            }
            return progress;
        }

        void fallAsleep()
        {
            _stateUpdater(FlasherState::FallAsleep);
            if (_udsFlasherParameters.attachRunningSbl) {
                LOG(INFO) << "Attaching to already running UDS SBL, skipping fallAsleep";
                return;
            }
            if (_udsFlasherParameters.skipFallAsleep) {
                LOG(INFO) << "Fall asleep skipped, vehicle programming mode was prepared by CEM";
                return;
            }
            if (!common::UDSProtocolCommonSteps::fallAsleep(_channels)) {
                setFailed("Fall asleep failed");
            }
        }

        void keepAlive()
        {
            if (_udsFlasherParameters.attachRunningSbl) {
                return;
            }
            auto& channel{ common::getChannelByEcuId(_flasherParameters.carPlatform, _flasherParameters.ecuId, _channels) };
            common::UDSProtocolCommonSteps::keepAlive(channel);
        }

        void authorize()
        {
            _stateUpdater(FlasherState::Authorize);
            if (_udsFlasherParameters.attachRunningSbl) {
                return;
            }
            auto& channel{ common::getChannelByEcuId(_flasherParameters.carPlatform, _flasherParameters.ecuId, _channels) };
            try {
                common::UDSRequest programmingSession{ _canId, { 0x10, 0x02 } };
                programmingSession.process(channel, { 0x02 }, 1, 1000);
            }
            catch (const std::exception& ex) {
                LOG(WARNING) << "Programming session request before authorize failed: " << ex.what();
            }
            if (!common::UDSProtocolCommonSteps::authorize(channel, _canId, _udsFlasherParameters.pin)) {
                setFailed("Authorization failed");
            }
        }

        void loadBootloader()
        {
            _stateUpdater(FlasherState::LoadBootloader);
            if (_udsFlasherParameters.attachRunningSbl) {
                return;
            }
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
            if (_udsFlasherParameters.attachRunningSbl) {
                auto& channel{ common::getChannelByEcuId(_flasherParameters.carPlatform, _flasherParameters.ecuId, _channels) };
                if (!authorizeRunningSbl(channel)) {
                    setFailed("SBL attach authorization failed");
                }
            }
            else if (_flasherParameters.sblProvider) {
                const auto bootloader{ _flasherParameters.sblProvider->getSBL(
                    _flasherParameters.carPlatform, _flasherParameters.ecuId, _flasherParameters.additionalData) };
                auto& channel{ common::getChannelByEcuId(_flasherParameters.carPlatform, _flasherParameters.ecuId, _channels) };
                if (!common::UDSProtocolCommonSteps::startRoutine(channel, _canId, bootloader.header.call)) {
                    setFailed("Bootloader starting failed");
                    return;
                }
                if (!authorizeRunningSbl(channel)) {
                    setFailed("SBL post-start authorization failed");
                }
            }
        }

        void writeFlash()
        {
            auto& channel{ common::getChannelByEcuId(_flasherParameters.carPlatform, _flasherParameters.ecuId, _channels) };
            _stateUpdater(FlasherState::EraseFlash);
            if (!common::UDSProtocolCommonSteps::eraseFlash(channel, _canId, _flasherParameters.flash)) {
                setFailed("Flash erasing failed");
                return;
            }
            for(const auto& chunk: _flasherParameters.flash.chunks) {
                _stateUpdater(FlasherState::WriteFlash);
                bool chunkWritten = false;
                for (size_t attempt = 1; attempt <= 2 && !chunkWritten; ++attempt) {
                    if (attempt > 1) {
                        LOG(WARNING) << "Retry transferChunk attempt=" << attempt
                                     << " offset=0x" << std::hex << chunk.writeOffset;
                    }
                    chunkWritten = common::UDSProtocolCommonSteps::transferChunk(channel, _canId, chunk,
                                                                                 _progressUpdater);
                }
                if (!chunkWritten) {
                    setFailed("Flash writing failed");
                    return;
                }
            }
        }

        void checkValidApplication()
        {
            auto& channel{ common::getChannelByEcuId(_flasherParameters.carPlatform, _flasherParameters.ecuId, _channels) };
            if (!common::UDSProtocolCommonSteps::checkValidApplication(channel, _canId)) {
                setFailed("Application validation failed");
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
        bool authorizeRunningSbl(const j2534::J2534Channel& channel)
        {
            return common::UDSProtocolCommonSteps::authorize(channel, _canId, _udsFlasherParameters.pin);
        }

        void setFailed(const std::string& message)
        {
            _isFailed = true;
            _errorMessage = message;
            LOG(ERROR) << message;
            _errorUpdater(message);
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
        const std::function<void(const std::string&)> _errorUpdater;
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

    UDSFlasher::UDSFlasher(j2534::J2534& j2534, FlasherParameters&& flasherParameters, UDSFlasherParameters&& udsFlasherParameters)
        : FlasherBase{ j2534, std::move(flasherParameters) }
        , _udsFlasherParameters{ std::move(udsFlasherParameters) }
    {
    }

    UDSFlasher::~UDSFlasher()
    {
    }

    std::vector<std::unique_ptr<j2534::J2534Channel>> UDSFlasher::openChannels()
    {
        LOG(INFO) << "UDSFlasher opening target ECU channel only, ecu=0x" << std::hex
            << getFlasherParameters().ecuId;
        std::vector<std::unique_ptr<j2534::J2534Channel>> channels;
        auto channel = openChannelForEcu(getFlasherParameters().ecuId);
        if (!channel) {
            throw std::runtime_error("Failed to open UDS target ECU channel");
        }
        channels.emplace_back(std::move(channel));
        LOG(INFO) << "UDSFlasher target ECU channel opened";
        return channels;
    }

    void UDSFlasher::startImpl(std::vector<std::unique_ptr<j2534::J2534Channel>>& channels)
    {
        LOG(INFO) << "UDSFlasher startImpl enter, channels=" << channels.size();
        const auto ecuInfo{ common::getEcuInfoByEcuId(getFlasherParameters().carPlatform,
            getFlasherParameters().ecuId) };

        LOG(INFO) << "UDSFlasher target CAN ID=0x" << std::hex << std::get<1>(ecuInfo).canId;
        UDSFlasherImpl impl(channels, getFlasherParameters(), _udsFlasherParameters,
            std::get<1>(ecuInfo).canId, [this](FlasherState state) {
            setCurrentState(state);
        },
            [this](size_t progress) {
                incCurrentProgress(progress);
            },
            [this](const std::string& error) {
                setLastError(error);
            });

        LOG(INFO) << "UDSFlasher calculating maximum progress";
        setMaximumProgress(impl.getMaximumProgress());
        LOG(INFO) << "UDSFlasher maximum progress=" << std::dec << getMaximumProgress();

        LOG(INFO) << "UDSFlasher FSM enter";
        FSM::Instance fsm{ impl };

        while(getCurrentState() != FlasherState::Done && getCurrentState() != FlasherState::Error) {
            fsm.update();
        }
        LOG(INFO) << "UDSFlasher FSM exit, state=" << static_cast<int>(getCurrentState());
    }

} // namespace flasher
