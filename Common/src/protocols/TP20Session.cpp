#include "common/protocols/TP20Session.hpp"

#include "common/protocols/TP20Request.hpp"
#include "common/Util.hpp"

#define HFSM2_ENABLE_ALL
#include "common/hfsm2/machine.hpp"

namespace common {

    class TP20SessionImpl {
    public:
        enum class State {
            Disconnected,
            Idle,
            Busy,
            Error
        };

        TP20SessionImpl(j2534::J2534Channel& channel, CarPlatform carPlatform, uint8_t ecuId)
            : _channel{ channel }
            , _carPlatform{ carPlatform }
            , _ecuId{ ecuId }
            , _state{ State::Disconnected }
            , _rxId{ 0 }
            , _txId{ 0 }
            , _minimimSendDelay{ 0 }
            , _lastRequestTime{ 0 }
        {
        }

        State getCurrentState() const
        {
            return _state;
        }

        bool connect()
        {
            const uint32_t initialCanId{ 0x200 };
            const uint8_t setupReqOpCode{ 0xC0 };
            const auto ecuInfo{ getEcuInfoByEcuId(_carPlatform, _ecuId) };
            const auto ecuCanId{ std::get<1>(ecuInfo).canId };
            const uint16_t requestedChannel{ 0x300 };
            KWPRequest channelSetupRequest{ initialCanId, std::get<1>(ecuInfo).canId,
                                           { _ecuId, setupReqOpCode, 0x00, 0x10, (requestedChannel >> 8) & 0xFF, 0x03 & 0xFF, 0x01 } };
            const auto csResp{ channelSetupRequest.process(_channel) };
            if (csResp.size() < 6) {
                return false;
            }
            if (csResp[0] == 0 && csResp[1] == 0xD0 && encode(csResp[2], csResp[3]) == requestedChannel) {
                _txId = encode(csResp[4], csResp[5]);
            }
            _state = State::Idle;
            return true;
        }

        bool disconnect()
        {
            _state = State::Disconnected;
            return false;
        }

        void setRequestData(std::vector<uint8_t>&& request)
        {
            _dataToSend = std::move(request);
        }

        bool sendRequest()
        {
            _state = State::Busy;
            return false;
        }

        bool needSendMore()
        {
            return !_dataToSend.empty();
        }

        bool waitForAck()
        {
            return false;
        }

        bool readResponse()
        {
            return false;
        }

        bool needReadMore()
        {
            return false;
        }

        bool writeAck()
        {
            return false;
        }

        void idle()
        {
            _state = State::Idle;
        }

        void error()
        {
            _state = State::Error;
        }

        void reset()
        {
            _state = State::Idle;
            _dataToSend.clear();
            _receivedData.clear();
            _needReadMore = false;
        }

        std::vector<uint8_t>&& releaseReceivedData()
        {
            return std::move(_receivedData);
        }

    private:
        const j2534::J2534Channel& _channel;
        const CarPlatform _carPlatform;
        const uint8_t _ecuId;
        std::chrono::milliseconds _lastRequestTime;
        State _state;
        uint32_t _rxId;
        uint32_t _txId;
        uint8_t _minimimSendDelay;
        std::vector<uint8_t> _dataToSend;
        std::vector<uint8_t> _receivedData;
        bool _needReadMore;
    };

    using M = hfsm2::MachineT<hfsm2::Config::ContextT<TP20SessionImpl&>>;
    using FSM = M::PeerRoot<
        struct Idle,
        struct SendRequest,
        struct WaitForAck,
        struct ReadResponse,
        struct WriteAck,
        struct Error
        >;

    struct Disconnected : public FSM::State {
    };

    struct Idle : public FSM::State {
    public:
        void enter(PlanControl& control)
        {
            control.context().idle();
        }
    };

    struct SendRequest : public FSM::State {
    public:
        void update(FullControl& control)
        {
            if(control.context().sendRequest()) {
                control.changeTo<WaitForAck>();
            }
            else {
                control.changeTo<Error>();
            }
        }
    };

    struct WaitForAck : public FSM::State {
    public:
        void update(FullControl& control)
        {
            if(!control.context().waitForAck()) {
                control.changeTo<Error>();
            }
            else {
                if(control.context().needSendMore()) {
                    control.changeTo<SendRequest>();
                }
                else {
                    control.changeTo<ReadResponse>();
                }
            }
        }
    };

    struct ReadResponse : public FSM::State {
    public:
        void update(FullControl& control)
        {
            if(control.context().readResponse()) {
                control.changeTo<WriteAck>();
            }
            else {
                control.changeTo<Error>();
            }
        }
    };

    struct WriteAck : public FSM::State {
    public:
        void update(FullControl& control)
        {
            if(!control.context().writeAck()) {
                control.changeTo<Error>();
            }
            else {
                if(control.context().needReadMore()) {
                    control.changeTo<ReadResponse>();
                }
                else {
                    control.changeTo<Idle>();
                }
            }
        }
    };

    struct Error : public FSM::State {
    public:
        void enter(PlanControl& control)
        {
            control.context().error();
        }
    };

    TP20Session::TP20Session(j2534::J2534Channel& channel, CarPlatform carPlatform, uint8_t ecuId)
        : _impl{ std::make_unique<TP20SessionImpl>(channel, carPlatform, ecuId) }
    {
    }

    TP20Session::~TP20Session()
    {
        stop();
    }

    bool TP20Session::start()
    {
        return _impl->connect();
    }

    void TP20Session::stop()
    {
        _impl->disconnect();
    }

    std::vector<uint8_t> TP20Session::process(std::vector<uint8_t>&& request)
    {
        try {
            FSM::Instance fsm{ *_impl };
            _impl->setRequestData(std::move(request));
            fsm.immediateChangeTo<SendRequest>();
            while(_impl->getCurrentState() == TP20SessionImpl::State::Busy) {
                fsm.update();
            }
            return _impl->releaseReceivedData();
        }
        catch(...) {
            _impl->reset();
            return {};
        }
    }

} // namespace common
