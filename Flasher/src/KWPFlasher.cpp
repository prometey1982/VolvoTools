#include "flasher/KWPFlasher.hpp"

#include <j2534/J2534.hpp>
#include <j2534/J2534Channel.hpp>

#include <common/protocols/KWPProtocolCommonSteps.hpp>
#include <common/protocols/TP20RequestProcessor.hpp>
#include <common/protocols/UDSRequestProcessor.hpp>
#include <common/CommonData.hpp>
#include <common/Util.hpp>

#define HFSM2_ENABLE_ALL
#include <common/hfsm2/machine.hpp>

namespace flasher {

    class KWPFlasherImpl {
    public:
        KWPFlasherImpl(common::RequestProcessorBase& requestProcessor,
                       const FlasherParameters& flasherParameters,
                       const KWPFlasherParameters& kwpFlasherParameters,
                       uint32_t canId,
                       const std::function<void(FlasherState)>& stateUpdater,
                       const std::function<void(size_t)>& progressUpdater)
            : _requestProcessor{ requestProcessor }
            , _flasherParameters{ flasherParameters }
            , _kwpFlasherParameters{ kwpFlasherParameters }
            , _canId{ canId }
            , _isFailed{ false }
            , _stateUpdater{ stateUpdater }
            , _progressUpdater{ progressUpdater }
        {
        }

        size_t getMaximumProgress()
        {
            size_t bootloaderSize{};
            if (const auto sblProvider = _flasherParameters.sblProvider) {
                const auto bootloader{ sblProvider->getSBL(
                    _flasherParameters.carPlatform, _flasherParameters.ecuId, _flasherParameters.additionalData) };
                bootloaderSize = FlasherBase::getProgressFromVBF(bootloader);
            }
            return bootloaderSize  + FlasherBase::getProgressFromVBF(_flasherParameters.flash);
        }

        void authorize()
        {
            _stateUpdater(FlasherState::Authorize);
            if (!common::KWPProtocolCommonSteps::authorize(_requestProcessor, _kwpFlasherParameters.pin)) {
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

        void eraseFlash()
        {
            _stateUpdater(FlasherState::EraseFlash);
            if (!common::KWPProtocolCommonSteps::eraseFlash(_requestProcessor, _flasherParameters.flash)) {
                setFailed("Flash erasing failed");
            }
        }

        void writeFlash()
        {
            _stateUpdater(FlasherState::WriteFlash);
            if (!common::KWPProtocolCommonSteps::transferData(_requestProcessor, _flasherParameters.flash,
                                                              _progressUpdater)) {
                setFailed("Flash writing failed");
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
            _isFailed = true;
            _errorMessage = message;
        }

    private:
        common::RequestProcessorBase& _requestProcessor;
        const FlasherParameters& _flasherParameters;
        const KWPFlasherParameters& _kwpFlasherParameters;
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
            struct EraseFlash,
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
            plan.change<ProgrammingSession, EraseFlash>();
            plan.change<EraseFlash, WriteFlash>();
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
        FlasherParameters&& flasherParameters, KWPFlasherParameters&& kwpFlasherParameters)
        : FlasherBase{ j2534, std::move(flasherParameters) }
        , _kwpFlasherParameters{ std::move(kwpFlasherParameters) }
    {
    }

    KWPFlasher::~KWPFlasher()
    {
    }

    void KWPFlasher::startImpl(std::vector<std::unique_ptr<j2534::J2534Channel>>& channels)
    {
        const auto ecuInfo{ common::getEcuInfoByEcuId(getFlasherParameters().carPlatform,
            getFlasherParameters().ecuId) };

        const auto& channel{ common::getChannelByEcuId(getFlasherParameters().carPlatform, getFlasherParameters().ecuId, channels) };

        std::unique_ptr<common::TP20Session> tp20Session;
        std::unique_ptr<common::RequestProcessorBase> requestProcessor;
        switch (std::get<0>(ecuInfo).protocolId) {
        case ISO15765:
            requestProcessor = std::make_unique<common::UDSRequestProcessor>(channel, std::get<1>(ecuInfo).canId);
            break;
        case CAN:
            tp20Session = std::make_unique<common::TP20Session>(channel, getFlasherParameters().carPlatform, getFlasherParameters().ecuId);
            if (!tp20Session->start()) {
                throw std::runtime_error("Can't start TP20 session");
            }
            requestProcessor = std::make_unique<common::TP20RequestProcessor>(*tp20Session);
            break;
        default:
            throw std::runtime_error("Unsupported protocol for KWP flasher");
        }

        KWPFlasherImpl impl(*requestProcessor, getFlasherParameters(), _kwpFlasherParameters,
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
