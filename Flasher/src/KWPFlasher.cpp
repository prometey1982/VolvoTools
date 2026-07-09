#include "flasher/KWPFlasher.hpp"

#include <j2534/J2534.hpp>
#include <j2534/J2534Channel.hpp>

#include <common/protocols/KWPProtocolCommonSteps.hpp>
#include <common/protocols/TP20RequestProcessor.hpp>

#define LOG_MODULE_NAME "flasher"
#include <common/LogHelper.hpp>
#include <common/protocols/UDSRequestProcessor.hpp>
#include <common/ICanChannel.hpp>
#include <common/CommonData.hpp>
#include <common/Util.hpp>

#define HFSM2_ENABLE_ALL
#include <common/hfsm2/machine.hpp>

namespace flasher {

    class KWPFlasherImpl {
    public:
        KWPFlasherImpl(common::RequestProcessorBase& requestProcessor,
                       common::CarPlatform carPlatform,
                       uint32_t ecuId,
                       const KWPFlasherConfig& config,
                       uint32_t canId,
                       const std::function<void(FlasherState)>& stateUpdater,
                       const std::function<void(size_t)>& progressUpdater)
            : _requestProcessor{ requestProcessor }
            , _carPlatform{ carPlatform }
            , _ecuId{ ecuId }
            , _config{ config }
            , _canId{ canId }
            , _isFailed{ false }
            , _stateUpdater{ stateUpdater }
            , _progressUpdater{ progressUpdater }
        {
        }

        size_t getMaximumProgress()
        {
            return FlasherBase::getProgressFromVBF(_config.bootloader)  + FlasherBase::getProgressFromVBF(_config.flash);
        }

        void authorize()
        {
            _stateUpdater(FlasherState::Authorize);
            if (!common::KWPProtocolCommonSteps::authorize(_requestProcessor, _config.pin)) {
                setFailed("Authorization failed");
            }
        }

        void swithToProgramming()
        {
            _stateUpdater(FlasherState::ProgrammingSession);
            if (!common::KWPProtocolCommonSteps::enterProgrammingSession(_requestProcessor)) {
                setFailed("Failed to enter programming session");
            }
        }

        void writeFlash()
        {
            for (size_t i = 0; i < _config.flash.chunks.size(); ++i) {
                const auto& chunk = _config.flash.chunks[i];
                _stateUpdater(FlasherState::RequestDownload);
                const auto maxDownloadSize{ common::KWPProtocolCommonSteps::requestDownload(_requestProcessor, chunk) };
                if (!maxDownloadSize) {
                    setFailed("Request download failed");
                    break;
                }
                _stateUpdater(FlasherState::EraseFlash);
                if (!common::KWPProtocolCommonSteps::eraseFlash(_requestProcessor, chunk)) {
                    setFailed("Flash erasing failed");
                    break;
                }
                _stateUpdater(FlasherState::WriteFlash);
                if (!common::KWPProtocolCommonSteps::transferData(_requestProcessor, chunk,
                    maxDownloadSize, _progressUpdater)) {
                    setFailed("Flash writing failed");
                    break;
                }
            }
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
        common::RequestProcessorBase& _requestProcessor;
        common::CarPlatform _carPlatform;
        uint32_t _ecuId;
        const KWPFlasherConfig& _config;
        const uint32_t _canId;
        bool _isFailed;
        std::string _errorMessage;
        const std::function<void(FlasherState)> _stateUpdater;
        const std::function<void(size_t)> _progressUpdater;
    };

using M = hfsm2::MachineT<hfsm2::Config::ContextT<KWPFlasherImpl&>>;
    using FSM = M::PeerRoot<
        M::Composite<
            struct StartWork,
            struct Authorize,
            struct ProgrammingSession,
            struct WriteFlash>,
        struct Done,
        struct Error
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
            plan.change<Authorize, ProgrammingSession>();
            plan.change<ProgrammingSession, WriteFlash>();
        }

        void planSucceeded(FullControl& control) {
            control.changeTo<Done>();
        }

        void planFailed(FullControl& control)
        {
            control.changeTo<Error>();
        }
    };

    struct Authorize : public BaseState {
        void enter(PlanControl& control)
        {
            control.context().authorize();
        }
    };

    struct ProgrammingSession : public BaseState {
        void enter(PlanControl& control)
        {
            control.context().swithToProgramming();
        }
    };

    struct WriteFlash : public BaseState {
        void enter(PlanControl& control)
        {
            control.context().writeFlash();
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

    KWPFlasher::KWPFlasher(j2534::J2534& j2534,
        common::CarPlatform carPlatform, uint32_t ecuId,
        KWPFlasherConfig&& config)
        : FlasherBase{ j2534, carPlatform, ecuId }
        , _config{ std::move(config) }
    {
    }

    KWPFlasher::~KWPFlasher()
    {
    }

    void KWPFlasher::startImpl(std::vector<std::unique_ptr<ICanChannel>>& channels)
    {
        const auto ecuInfo{ common::getEcuInfoByEcuId(_carPlatform, _ecuId) };

        auto& channel{ common::getChannelByEcuId(_carPlatform, _ecuId, channels) };

        std::unique_ptr<common::TP20Session> tp20Session;
        std::unique_ptr<common::RequestProcessorBase> requestProcessor;
        switch (std::get<0>(ecuInfo).protocolId) {
        case ISO15765:
            requestProcessor = std::make_unique<common::UDSRequestProcessor>(channel, std::get<1>(ecuInfo).canId);
            break;
        case CAN:
            tp20Session = std::make_unique<common::TP20Session>(channel, _carPlatform, _ecuId);
            if (!tp20Session->start()) {
                throw std::runtime_error("Can't start TP20 session");
            }
            requestProcessor = std::make_unique<common::TP20RequestProcessor>(*tp20Session);
            break;
        default:
            throw std::runtime_error("Unsupported protocol for KWP flasher");
        }

        KWPFlasherImpl impl(*requestProcessor, _carPlatform, _ecuId, _config,
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
