#include "common/protocols/TP20Session.hpp"

#include "common/protocols/TP20Request.hpp"
#include "common/protocols/TP20Service.hpp"
#include "common/CanFrame.hpp"
#include "common/ICanChannel.hpp"
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

        TP20SessionImpl(ICanChannel& channel, CarPlatform carPlatform, uint8_t ecuId)
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
            , _readTimeout{ 1000 }
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
            if (!(csResp[0] == 0 && csResp[1] == TP20ServiceID::ChannelSetupPositiveResponse && encodeBigEndian(csResp[2], csResp[3]) == requestedChannel)) {
                return false;
            }
            const auto channelTxId{ encodeBigEndian(csResp[4], csResp[5]) };
            {
                const unsigned long passFilter = 0x00000001;
                unsigned long filterId;
                auto canIdVec = common::toVector(requestedChannel);
                _channel.startMsgFilter(passFilter,
                    {0, {0xFF, 0xFF, 0xFF, 0xFF}},
                    {0, std::move(canIdVec)},
                    nullptr, filterId);
            }
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
            unsigned long keepAliveId;
            _channel.startPeriodicMsg({_txId, {0xA3}}, 1000, keepAliveId);
            _keepAliveIds.push_back(keepAliveId);
            return true;
        }

        void disconnect()
        {
            for (auto id : _keepAliveIds) {
                _channel.stopPeriodicMsg(id);
            }
            _keepAliveIds.clear();
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

        void setReadTimeout(size_t readTimeout)
        {
            _readTimeout = readTimeout;
        }

        bool sendRequest()
        {
            if (_dataToSend.empty()) {
                return false;
            }
            auto payload{ _dataToSend.front() };
            _dataToSend.pop_front();
            payload[0] = _dataToSend.empty() ? 0x10 : _packetsTillAck > 1 ? 0x20 : 0x10;
            const auto result{ sendMessage(_sendPacketCounter++, std::move(payload)) };
            if (result) {
                _needReadAck = !_packetsTillAck || _dataToSend.empty();
            }
            return result;
        }

        bool readResponse()
        {
            try {
                while (true) {
                    CanFrame frame;
                    if (!_channel.receive(frame, _readTimeout)) {
                        throw std::runtime_error("read timeout");
                    }
                    if (checkMessageForSkip(frame)) {
                        continue;
                    }
                    if (frame.data.empty()) {
                        continue;
                    }
                    const auto op{ (frame.data[0] >> 4) & 0x0F };
                    _needReadMore = !(op & 0x1);
                    _needSendAck = !(op & 0x02);
                    const size_t dataOffset = _receivedData.empty() ? 3 : 1;
                    if (frame.data.size() < dataOffset) {
                        continue;
                    }
                    _receivedData.reserve(_receivedData.size() + frame.data.size() - dataOffset);
                    std::copy(frame.data.cbegin() + dataOffset, frame.data.cend(), std::back_inserter(_receivedData));
                    if (_needSendAck) {
                        _ackPacketCounter = (frame.data[0] & 0x0F) + 1;
                    }
                    if (!_needReadMore && !_needSendAck) {
                        break;
                    }
                }
                return true;
            }
            catch (...) {
                return false;
            }
        }

        bool readAck()
        {
            try {
                while (true) {
                    CanFrame frame;
                    if (!_channel.receive(frame, 10000)) {
                        throw std::runtime_error("ack timeout");
                    }
                    if (checkMessageForSkip(frame)) {
                        continue;
                    }
                    if (!frame.data.empty() && (frame.data[0] & 0xF0) == 0xB0) {
                        _needReadAck = false;
                        _packetsTillAck = _maxPacketsTillAck;
                        return true;
                    }
                }
            }
            catch (...) {
                return false;
            }
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
            _needSendAck = false;
            _needReadAck = false;
        }

        std::vector<uint8_t>&& releaseReceivedData()
        {
            return std::move(_receivedData);
        }

    private:
        bool checkMessageForSkip(const CanFrame& frame)
        {
            if (frame.data.size() < 1) {
                return true;
            }
            if (frame.id != _rxId) {
                return true;
            }
            if (frame.data[0] == 0xA1) {
                return true;
            }
            if (frame.data.size() >= 3 && frame.data[0] == 0x7F && frame.data[2] == 0x78) {
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
            _lastRequestTime = now;
            return _channel.send({_txId, std::move(payload)});
        }

    private:
        ICanChannel& _channel;
        const CarPlatform _carPlatform;
        const uint8_t _ecuId;
        std::chrono::steady_clock::duration _lastRequestTime;
        uint32_t _rxId;
        uint32_t _txId;
        std::vector<unsigned long> _keepAliveIds;
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
        size_t _readTimeout;
    };

    using M = hfsm2::MachineT<hfsm2::Config::ContextT<TP20SessionImpl&>>;
    using FSM = M::PeerRoot<
        struct Idle,
        struct SendRequest,
        struct WaitForAck,
        struct ReadResponseState,
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
                control.changeTo<Idle>();
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
                    control.changeTo<Idle>();
                }
            }
        }
    };

    struct ReadResponseState : public FSM::State {
    public:
        void update(FullControl& control)
        {
            if(!control.context().readResponse()) {
                control.changeTo<Error>();
            }
            else if(control.context().needSendAck()) {
                control.changeTo<WriteAck>();
            }
            else if(control.context().needReadMore()) {
            }
            else {
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
            else if(control.context().needReadMore()) {
                control.changeTo<ReadResponseState>();
            }
            else {
                control.changeTo<Idle>();
            }
        }
    };

    struct Error : public FSM::State {
    public:
        void update(FullControl& control)
        {
            control.context().error();
        }
    };

    TP20Session::TP20Session(ICanChannel& channel, CarPlatform carPlatform, uint8_t ecuId)
        : _impl{ std::make_unique<TP20SessionImpl>(channel, carPlatform, ecuId) }
    {
    }

    TP20Session::~TP20Session() = default;

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
        _impl->reset();
        _impl->setRequestData(request);

        FSM::Instance fsm{ *_impl };
        while(!_impl->needSendMore() && !_impl->needSendAck() && !_impl->needReadMore() && !_impl->needReadAck()) {
            fsm.update();
        }

        auto result{ _impl->releaseReceivedData() };
        return result;
    }

    bool TP20Session::writeMessage(const std::vector<uint8_t>& request) const
    {
        _impl->setRequestData(request);
        return _impl->sendRequest();
    }

    std::vector<uint8_t> TP20Session::readMessage(size_t timeout) const
    {
        _impl->setReadTimeout(timeout);
        _impl->readResponse();
        return _impl->releaseReceivedData();
    }

} // namespace common
