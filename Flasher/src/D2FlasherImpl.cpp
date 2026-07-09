#include "D2FlasherImpl.hpp"

#include "flasher/FlasherBase.hpp"
#include "common/ICanChannel.hpp"
#include <common/Util.hpp>
#include <common/protocols/D2ProtocolCommonSteps.hpp>

#define HFSM2_ENABLE_ALL
#include <common/hfsm2/machine.hpp>

#define LOG_MODULE_NAME "flasher"
#include <common/LogHelper.hpp>

namespace flasher {

D2FlasherImpl::D2FlasherImpl(const std::vector<std::unique_ptr<ICanChannel>>& channels,
                               common::CarPlatform carPlatform,
                               uint8_t ecuId,
                               const common::VBF& bootloader,
                               const std::function<void(FlasherState)>& stateUpdater,
                               const std::function<void(size_t)>& progressUpdater,
                               const std::function<void(ICanChannel&, uint8_t)>& eraseCallback,
                               const std::function<void(ICanChannel&, uint8_t)>& writeCallback)
    : _channels{ channels }
    , _carPlatform{ carPlatform }
    , _ecuId{ ecuId }
    , _bootloader{ bootloader }
    , _stateUpdater{ stateUpdater }
    , _progressUpdater{ progressUpdater }
    , _eraseCallback{ eraseCallback }
    , _writeCallback{ writeCallback }
{
}

size_t D2FlasherImpl::getMaximumProgress() const
{
    const size_t stepCost = 100;
    return stepCost * 4 + FlasherBase::getProgressFromVBF(_bootloader) + getMaximumFlashProgressValue();
}

void D2FlasherImpl::setMaximumFlashProgressValue(size_t value)
{
    _maximumFlashProgress = value;
}

size_t D2FlasherImpl::getMaximumFlashProgressValue() const
{
    return _maximumFlashProgress;
}

void D2FlasherImpl::wakeUpChannels()
{
    _stateUpdater(FlasherState::WakeUp);
    common::D2ProtocolCommonSteps::wakeUp(_channels);
}

void D2FlasherImpl::fallAsleep()
{
    _stateUpdater(FlasherState::FallAsleep);
    if (!common::D2ProtocolCommonSteps::fallAsleep(_channels)) {
        setFailed("Fall asleep failed");
    }
}

void D2FlasherImpl::startPBL()
{
    _stateUpdater(FlasherState::OpenChannels);
    auto& channel{ common::getChannelByEcuId(_carPlatform, _ecuId, _channels) };
    if (!common::D2ProtocolCommonSteps::startPBL(channel, _ecuId)) {
        setFailed("Start PBL failed");
    }
}

void D2FlasherImpl::loadSBL()
{
    if (!isSBLRequired()) {
        return;
    }
    _stateUpdater(FlasherState::LoadBootloader);
    auto& channel{ common::getChannelByEcuId(_carPlatform, _ecuId, _channels) };
    if (!common::D2ProtocolCommonSteps::transferData(channel, _ecuId,
                                                      _bootloader, _progressUpdater)) {
        setFailed("SBL loading failed");
    }
}

void D2FlasherImpl::startSBL()
{
    if (!isSBLRequired()) {
        return;
    }
    _stateUpdater(FlasherState::StartBootloader);
    auto& channel{ common::getChannelByEcuId(_carPlatform, _ecuId, _channels) };
    if (!common::D2ProtocolCommonSteps::startRoutine(channel, _ecuId,
                                                      _bootloader.header.call)) {
        setFailed("SBL start failed");
    }
}

void D2FlasherImpl::eraseFlash()
{
    _stateUpdater(FlasherState::EraseFlash);
    auto& channel{ common::getChannelByEcuId(_carPlatform, _ecuId, _channels) };
    try {
        _eraseCallback(channel, _ecuId);
    }
    catch (...) {
        setFailed("Erase flash failed");
    }
}

void D2FlasherImpl::writeFlash()
{
    _stateUpdater(FlasherState::WriteFlash);
    auto& channel{ common::getChannelByEcuId(_carPlatform, _ecuId, _channels) };
    try {
        _writeCallback(channel, _ecuId);
    }
    catch (...) {
        setFailed("Write flash failed");
    }
}

void D2FlasherImpl::wakeUpFinish()
{
    _stateUpdater(FlasherState::WakeUp);
    common::D2ProtocolCommonSteps::wakeUp(_channels);
}

void D2FlasherImpl::setDIMTime()
{
    common::D2ProtocolCommonSteps::setDIMTime(_channels);
}

void D2FlasherImpl::done()
{
    _stateUpdater(FlasherState::Done);
    _isDone = true;
}

void D2FlasherImpl::error()
{
    _stateUpdater(FlasherState::Error);
    _isDone = true;
    _isFailed = true;
}

bool D2FlasherImpl::isFailed() const
{
    return _isFailed;
}

bool D2FlasherImpl::isSBLRequired() const
{
    return !_bootloader.chunks.empty();
}

void D2FlasherImpl::setFailed(const std::string& message)
{
    LOG_MODULE(ERROR) << message;
    _isFailed = true;
    _errorMessage = message;
}

// hFSM2 machine definition

using M = hfsm2::MachineT<hfsm2::Config::ContextT<D2FlasherImpl&>>;
using FSM = M::PeerRoot<
    M::Composite<
        struct StartWork,
        struct WakeUpChannels,
        struct FallAsleep,
        struct StartPBL,
        struct LoadSBL,
        struct StartSBL,
        struct EraseFlash,
        struct WriteFlash>,
    M::Composite<
        struct Finish,
        struct WakeUpFinish,
        struct SetDIMTime,
        struct Done,
        struct Error>
    >;

struct BaseState : public FSM::State {
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
    void update(FullControl& control)
    {
        control.succeed();
    }
};

struct StartWork : public FSM::State {
    void enter(PlanControl& control)
    {
        auto plan = control.plan();
        plan.change<WakeUpChannels, FallAsleep>();
        plan.change<FallAsleep, StartPBL>();
        plan.change<StartPBL, LoadSBL>();
        plan.change<LoadSBL, StartSBL>();
        plan.change<StartSBL, EraseFlash>();
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

struct WakeUpChannels : public BaseState {
    void enter(PlanControl& control)
    {
        control.context().wakeUpChannels();
    }
};

struct FallAsleep : public BaseState {
    void enter(PlanControl& control)
    {
        control.context().fallAsleep();
    }
};

struct StartPBL : public BaseState {
    void enter(PlanControl& control)
    {
        control.context().startPBL();
    }
};

struct LoadSBL : public BaseState {
    void enter(PlanControl& control)
    {
        control.context().loadSBL();
    }
};

struct StartSBL : public BaseState {
    void enter(PlanControl& control)
    {
        control.context().startSBL();
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
            plan.change<WakeUpFinish, SetDIMTime>();
            plan.change<SetDIMTime, Error>();
        }
        else {
            plan.change<WakeUpFinish, SetDIMTime>();
            plan.change<SetDIMTime, Done>();
        }
    }
};

struct WakeUpFinish : public BaseSuccesState {
    void enter(PlanControl& control)
    {
        control.context().wakeUpFinish();
    }
};

struct SetDIMTime : public BaseSuccesState {
    void enter(PlanControl& control)
    {
        control.context().setDIMTime();
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

void D2FlasherImpl::run()
{
    _isDone = false;
    _isFailed = false;
    FSM::Instance fsm{ *this };
    while (!_isDone) {
        fsm.update();
    }
}

} // namespace flasher
