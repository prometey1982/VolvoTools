#include "common/UDSPinFinder.hpp"

#include "common/UDSProtocolCommonSteps.hpp"
#include "common/Util.hpp"

#define HFSM2_ENABLE_ALL
#include "common/hfsm2/machine.hpp"

#include <array>
#include <optional>

namespace common {

    struct FinderData {
        FinderData(const CarPlatform carPlatform, const uint8_t ecuId,
            const std::function<void(UDSPinFinder::State, uint64_t)> stateCallback,
            const UDSPinFinder::Direction direction, const uint64_t startPin)
            : carPlatform{ carPlatform }
            , ecuId{ ecuId }
            , stateCallback{ stateCallback }
            , direction{ direction }
            , startPin{ startPin }
        {
        }

        const CarPlatform carPlatform;
        const uint8_t ecuId;
        const std::function<void(UDSPinFinder::State, uint64_t)> stateCallback;
        const UDSPinFinder::Direction direction;
        const uint64_t startPin;
    };

    class UDSPinFinderImpl {
        static uint32_t getCanId(const FinderData& finderData)
        {
            const auto ecuInfo{ getEcuInfoByEcuId(finderData.carPlatform, finderData.ecuId) };
            return std::get<1>(ecuInfo).canId;
        }

    public:
        UDSPinFinderImpl(const std::vector<std::unique_ptr<j2534::J2534Channel>>& channels,
            const FinderData& finderData)
            : _channels{ channels }
            , _finderData{ finderData }
            , _canId{ getCanId(finderData) }
            , _currentState{ UDSPinFinder::State::Initial }
            , _currentPin{ finderData.startPin }
            , _stop{ false }
            , _isFailed{ false }
        {
        }

        UDSPinFinder::State getCurrentState() const
        {
            std::unique_lock<std::mutex> lock{ _mutex };
            return _currentState;
        }

        bool isStopped() const
        {
            std::unique_lock<std::mutex> lock{ _mutex };
            return _currentState == UDSPinFinder::State::Error || _currentState == UDSPinFinder::State::Done;
        }

        std::optional<uint64_t> getFoundPin() const
        {
            std::unique_lock<std::mutex> lock{ _mutex };
            return _foundPin;
        }

        void fallAsleep()
        {
            setCurrentState(UDSPinFinder::State::FallAsleep);
            if (!common::UDSProtocolCommonSteps::fallAsleep(_channels)) {
                setFailed();
            }
        }

        void keepAlive()
        {
            setCurrentState(UDSPinFinder::State::KeepAlive);
            auto& channel{ getChannelByEcuId(_finderData.carPlatform, _finderData.ecuId, _channels) };
            UDSProtocolCommonSteps::keepAlive(channel);
        }

        bool authorize()
        {
            setCurrentState(UDSPinFinder::State::Work);
            auto& channel{ getChannelByEcuId(_finderData.carPlatform, _finderData.ecuId, _channels) };

            if (!UDSProtocolCommonSteps::authorize(channel, _canId, getPinArray(_currentPin))) {
                _currentPin += _finderData.direction == UDSPinFinder::Direction::Up ? 1 : -1;
                return false;
            }
            else {
                _foundPin = _currentPin;
                return true;
            }
        }

        void wakeUp()
        {
            setCurrentState(UDSPinFinder::State::WakeUp);
            common::UDSProtocolCommonSteps::wakeUp(_channels);
        }

        void done()
        {
            setCurrentState(UDSPinFinder::State::Done);
        }

        void error()
        {
            setCurrentState(UDSPinFinder::State::Error);
        }

        bool isFailed() const
        {
            return _isFailed;
        }

        bool isStopping() const
        {
            std::unique_lock<std::mutex> lock{ _mutex };
            return _stop;
        }

        void stop()
        {
            std::unique_lock<std::mutex> lock{ _mutex };
            _stop = true;
        }

    private:
        void setCurrentState(UDSPinFinder::State newState)
        {
            {
                std::unique_lock<std::mutex> lock{ _mutex };
                _currentState = newState;
            }
            callCallback();
        }

        void callCallback()
        {
            UDSPinFinder::State currentState;
            uint64_t currentPin;
            {
                std::unique_lock<std::mutex> lock{ _mutex };
                currentState = _currentState;
                currentPin = _currentPin;
            }
            if (_finderData.stateCallback) {
                _finderData.stateCallback(currentState, currentPin);
            }
        }

        void setFailed()
        {
            _isFailed = true;
        }

    private:
        const std::vector<std::unique_ptr<j2534::J2534Channel>>& _channels;
        const FinderData& _finderData;
        uint32_t _canId;
        UDSPinFinder::State _currentState;
        mutable std::mutex _mutex;
        uint64_t _currentPin;
        bool _stop;
        bool _isFailed;
        std::optional<uint64_t> _foundPin;

    };

    using M = hfsm2::MachineT<hfsm2::Config::ContextT<UDSPinFinderImpl&>>;
    using FSM = M::PeerRoot<
        M::Composite<
        struct StartWork,
        struct FallAsleep,
        struct KeepAlive,
        struct Authorize>,
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
        }

        void update(FullControl& control)
        {
            if (control.context().authorize()) {
                control.succeed();
            }
            if (control.context().isStopping()) {
                control.succeed();
            }
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

    UDSPinFinder::UDSPinFinder(j2534::J2534& j2534, CarPlatform carPlatform, uint8_t ecuId,
        const std::function<void(State, uint64_t)> stateCallback,
        Direction direction, uint64_t startPin)
        : _channelProvider{ j2534, carPlatform }
        , _data{ std::make_unique<FinderData>(carPlatform, ecuId, stateCallback, direction, startPin) }
        , _thread{}
    {
    }

    UDSPinFinder::~UDSPinFinder()
    {
        if (_thread.joinable()) {
            _thread.join();
        }
    }

    UDSPinFinder::State UDSPinFinder::getCurrentState() const
    {
        if (_impl) {
            return _impl->getCurrentState();
        }
        return State::Initial;
    }

    std::optional<uint64_t> UDSPinFinder::getFoundPin() const
    {
        if (_impl) {
            return _impl->getFoundPin();
        }
        return {};
    }

    bool UDSPinFinder::start()
    {
        if (_impl) {
            return false;
        }
        _thread = std::thread([this] {
            auto channels{ _channelProvider.getAllChannels(_data->ecuId) };
            _impl = std::make_unique<UDSPinFinderImpl>(channels, *_data);
            FSM::Instance fsm{ *_impl };

            while(!_impl->isStopped()) {
                fsm.update();
            }
        });
        return true;
    }

    void UDSPinFinder::stop()
    {
        if (_impl) {
            _impl->stop();
        }
    }


} // namespace common
