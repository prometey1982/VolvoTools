#include "common/protocols/TP20Session.hpp"

#include "common/protocols/TP20Request.hpp"
#include "common/protocols/TP20Service.hpp"
#include "common/CanMessage.hpp"
#include "common/Util.hpp"

#define HFSM2_ENABLE_ALL
#include "common/hfsm2/machine.hpp"

#include <deque>
#include <thread>
#include <iterator>

namespace common {

    class TP20SessionImpl {
    public:
        enum class State {
            Disconnected,
            Idle,
            Busy,
            Error
        };

        using PayloadT = std::vector<uint8_t>;

        TP20SessionImpl(j2534::J2534Channel& channel, CarPlatform carPlatform, uint8_t ecuId)
            : _channel{ channel }
            , _carPlatform{ carPlatform }
            , _ecuId{ ecuId }
            , _lastRequestTime{ 0 }
            , _rxId{ 0 }
            , _txId{ 0 }
            , _minimimSendDelay{ 0 }
            , _maxPacketsTillAck{ 0 }
            , _packetsTillAck{ 0 }
            , _sendPacketCounter{ 0 }
            , _ackPacketCounter{ 0 }
            , _receivePacketCounter{ 0 }
            , _needReadMore{ false }
            , _needSendAck{ false }
            , _needReadAck{ false }
        {
        }

        bool connect()
        {
            const uint32_t initialCanId{ 0x200 };
            const auto ecuInfo{ getEcuInfoByEcuId(_carPlatform, _ecuId) };
            const auto ecuCanId{ std::get<1>(ecuInfo).canId };
            const uint16_t requestedChannel{ 0x300 };
            TP20Request channelSetupRequest{ initialCanId, std::get<1>(ecuInfo).canId,
                                           { _ecuId, TP20ServiceID::ChannelSetup, 0x00, 0x10, requestedChannel & 0xFF, (requestedChannel >> 8) & 0xFF, 0x01 } };
            const auto csResp{ channelSetupRequest.process(_channel) };
            if (csResp.size() < 6) {
                return false;
            }
            if (!(csResp[0] == 0 && csResp[1] == TP20ServiceID::ChannelSetupPositiveResponse && encode(csResp[2], csResp[3]) == requestedChannel)) {
                return false;
            }
            const auto channelTxId{ encode(csResp[4], csResp[5]) };
            prepareTP20Channel(_channel, requestedChannel);
            TP20Request channelParametersRequest{ channelTxId, requestedChannel,
                                                { TP20ServiceID::SetupChannelParameters, 0xF, 0x8A, 0xFF, 0x32, 0xFF } };
            const auto cpResp{ channelParametersRequest.process(_channel, 2000) };
            if (cpResp.size() < 6 || cpResp[0] != TP20ServiceID::SetupChannelParametersPositiveResponse) {
                return false;
            }
            _txId = channelTxId;
            _rxId = requestedChannel;
            _sendPacketCounter = 0;
            _receivePacketCounter = 0;
            _maxPacketsTillAck = cpResp[1];
            _minimimSendDelay = (cpResp[4] & 0x3F);
            switch ((cpResp[4] >> 6)) {
            case 0:
                _minimimSendDelay *= 0.1;
                break;
            case 1:
                break;
            case 2:
                _minimimSendDelay *= 10;
                break;
            case 3:
                _minimimSendDelay *= 100;
            }
            _channel.startPeriodicMsgs(common::TP20Message(_txId, { 0xA3 }), 1000);
            return true;
        }

        bool disconnect()
        {
            return false;
        }

        void setRequestData(const std::vector<uint8_t>& request)
        {
            if (request.size() > 4096) {
                throw std::runtime_error("Can't send request. Datasize too long");
            }
            _dataToSend.clear();
            PayloadT payload{ 0, (request.size() >> 8) & 0xFF, request.size() & 0xFF };
            const size_t maxPayloadSize{ 8 };
            size_t payloadOffset{ 3 };
            size_t remainingSize{ maxPayloadSize - payloadOffset };
            for (size_t i = 0; i < request.size(); i += remainingSize) {
                const auto currentRequestOffset{ request.data() + i };
                remainingSize = std::min(maxPayloadSize - payloadOffset, request.size() - i);
                std::copy(currentRequestOffset, currentRequestOffset + remainingSize, std::back_inserter(payload));
                _dataToSend.emplace_back(std::move(payload));
                payloadOffset = 1;
                payload = PayloadT(payloadOffset);
            }
        }

        bool sendRequest()
        {
            if (_dataToSend.empty()) {
                return false;
            }
            auto payload{ _dataToSend.front() };
            _dataToSend.pop_front();
            payload[0] = _dataToSend.empty() ? 0x10 : _packetsTillAck > 0 ? 0x20 : 0x10;
            const auto result{ sendMessage(_sendPacketCounter++, std::move(payload)) };
            if (result) {
                --_packetsTillAck;
                _needReadAck = !_packetsTillAck || _dataToSend.empty();
            }
            return result;
        }

        bool readResponse()
        {
            try {
                _channel.readMsgs([this](const uint8_t* data, size_t length) {
                    if (checkMessageForSkip(data, length)) {
                        return true;
                    }
                    const auto op{ (data[4] >> 4) & 0x0F };
                    _needReadMore = !(op & 0x1);
                    _needSendAck = !(op & 0x02);
                    _receivedData.reserve(_receivedData.size() + length - 5);
                    std::copy(data + 5, data + length, std::back_inserter(_receivedData));
                    if (_needSendAck) {
                        _ackPacketCounter = (data[4] & 0x0F) + 1;
                    }
                    return _needReadMore && !_needSendAck;
                    }, 5000);
                return true;
            }
            catch (...) {
                return false;
            }
        }

        bool readAck()
        {
            _channel.readMsgs([this](const uint8_t* data, size_t length) {
                if (checkMessageForSkip(data, length)) {
                    return true;
                }
                if ((data[4] & 0xF0) == 0xB0) {
                    _needReadAck = false;
                    _packetsTillAck = _maxPacketsTillAck;
                    return false;
                }
                return true;
            });
            return !_needReadAck;
        }

        bool sendAck()
        {
            return sendMessage(_ackPacketCounter, { 0xB0 });
        }

        bool needSendMore()
        {
            return !_dataToSend.empty();
        }

        bool needSendAck()
        {
            return _needSendAck;
        }

        bool needReadMore()
        {
            return _needReadMore;
        }

        bool needReadAck()
        {
            return _needReadAck;
        }

        void idle()
        {
        }

        void error()
        {
        }

        void reset()
        {
            _dataToSend.clear();
            _receivedData.clear();
            _needReadMore = false;
        }

        std::vector<uint8_t>&& releaseReceivedData()
        {
            return std::move(_receivedData);
        }

    private:
        bool checkMessageForSkip(const uint8_t* data, size_t dataSize)
        {
            if (dataSize < 5) {
                return true;
            }
            if (encode(data[3], data[2], data[1], data[0]) != _rxId) {
                return true;
            }
            if (data[4] == 0xA1) {
                return true;
            }
            return false;
        }

        bool sendMessage(uint8_t packetCounter, PayloadT&& payload)
        {
            const auto now{ std::chrono::steady_clock::now().time_since_epoch()};
            const auto nextCommandDuration{ _lastRequestTime + std::chrono::milliseconds(_minimimSendDelay) };
            if (nextCommandDuration > now) {
                std::this_thread::sleep_for((nextCommandDuration - now));
            }
            payload[0] |= packetCounter & 0x0F;
            TP20Message canMessage{ _txId, std::move(payload) };
            unsigned long msgsNum = 1;
            return _channel.writeMsgs(canMessage, msgsNum) == STATUS_NOERROR;
        }

    private:
        const j2534::J2534Channel& _channel;
        const CarPlatform _carPlatform;
        const uint8_t _ecuId;
        std::chrono::milliseconds _lastRequestTime;
        uint32_t _rxId;
        uint32_t _txId;
        uint32_t _minimimSendDelay;
        uint8_t _maxPacketsTillAck;
        uint8_t _packetsTillAck;
        uint8_t _sendPacketCounter;
        uint8_t _ackPacketCounter;
        uint8_t _receivePacketCounter;
        std::deque<PayloadT> _dataToSend;
        std::vector<uint8_t> _receivedData;
        bool _needReadMore;
        bool _needSendAck;
        bool _needReadAck;
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
            if (!control.context().sendRequest()) {
                control.changeTo<Error>();
            }
            else if(control.context().needReadAck()) {
                control.changeTo<WaitForAck>();
            }
            else if(!control.context().needSendMore()) {
                control.changeTo<ReadResponse>();
            }
        }
    };

    struct WaitForAck : public FSM::State {
    public:
        void update(FullControl& control)
        {
            if(!control.context().readAck()) {
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
            if(!control.context().readResponse()) {
                control.changeTo<Error>();
            }
            else if(control.context().needSendAck()) {
                control.changeTo<WriteAck>();
            }
            else if (!control.context().needReadMore()) {
                control.changeTo<Idle>();
            }
        }
    };

    struct WriteAck : public FSM::State {
    public:
        void update(FullControl& control)
        {
            if(!control.context().sendAck()) {
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

    std::vector<uint8_t> TP20Session::process(const std::vector<uint8_t>& request) const
    {
        try {
            FSM::Instance fsm{ *_impl };
            _impl->setRequestData(request);
            fsm.immediateChangeTo<SendRequest>();
            while (!fsm.isActive<Idle>() && !fsm.isActive<Error>()) {
                fsm.update();
            }
            if (fsm.isActive<Error>()) {
                return {};
            }
            return _impl->releaseReceivedData();
        }
        catch(...) {
            _impl->reset();
            return {};
        }
    }

} // namespace common
