#include "CanMessagesTransceiver.hpp"

#include "CEMCanMessage.hpp"

#include "../j2534/J2534Channel.hpp"

namespace common {

CanMessagesTransceiver::CanMessagesTransceiver(std::unique_ptr<j2534::J2534Channel> j2534Channel,
                                               unsigned long protocolID,
                                               unsigned long txFlags)
    : _j2534Channel{std::move(j2534Channel)}
    , _protocolID{protocolID}
    , _txFlags{txFlags}
    , _isReadEnabled{false}
    , _isShutdown{false}
    , _thread(&CanMessagesTransceiver::readThread, this)
{
}

CanMessagesTransceiver::~CanMessagesTransceiver()
{
    {
        std::unique_lock<std::mutex> lock{_mutex};
        _isShutdown = true;
        _cond.notify_all();
    }
    _thread.join();
}

void CanMessagesTransceiver::subscribe(ECUType ecuType, ICanMessagesReceiver& receiver)
{
    std::unique_lock<std::mutex> lock{_mutex};
    _subscribers.insert(std::make_pair(ecuType, &receiver));
}

void CanMessagesTransceiver::unsubscribeAll(const ICanMessagesReceiver& receiver)
{
    std::unique_lock<std::mutex> lock{_mutex};
    for(auto it = _subscribers.begin(); it != _subscribers.end();) {
        if(it->second == &receiver) {
            it = _subscribers.erase(it);
        }
        else {
            ++it;
        }
    }
}

void CanMessagesTransceiver::sendMessage(const std::vector<uint8_t>& data)
{
//    CEMCanMessages messages{{data}};
//    const auto& passThruMsgs{messages.toPassThruMsgs(_protocolID, _txFlags)};
//    unsigned long numMsgs = passThruMsgs.size();
//    _j2534Channel->writeMsgs(passThruMsgs, numMsgs);
}

void CanMessagesTransceiver::runRead(bool enabled)
{
    std::unique_lock<std::mutex> lock{_mutex};
    _isReadEnabled = enabled;
    _cond.notify_all();
}

void CanMessagesTransceiver::readThread()
{
    for(;;) {
        {
            std::unique_lock<std::mutex> lock{_mutex};
            _cond.wait(lock, [&]() {
                return _isShutdown || _isReadEnabled;
            });
            if(_isShutdown)
                break;
        }
        std::vector<PASSTHRU_MSG> msgs{1};
        if(_j2534Channel->readMsgs(msgs) == STATUS_NOERROR) {
            processMessages(msgs);
        }
    }
}

void CanMessagesTransceiver::processMessages(const std::vector<PASSTHRU_MSG>& msgs)
{
    for(auto it = msgs.cbegin(); it != msgs.cend(); ++it) {
        if(it->DataSize < 5)
            continue;
        auto ecuType = CEMCanMessage::getECUType(it->Data);
        uint8_t packetType = it->Data[4];
        // begin of packet
        if(packetType && 0x80) {
            _receivedMessages[ecuType] = { it->Data + 5, it->Data + it->DataSize };
        }
        else if(packetType && 0x40) {
            _receivedMessages[ecuType].insert(_receivedMessages[ecuType].end(), it->Data + 5, it->Data + it->DataSize);
        }
        const auto range = _subscribers.equal_range(ecuType);
        for(auto callback = range.first; callback != range.second; ++it) {
            callback->second->onCanMessage(&it->Data[4], it->DataSize - 4);
        }
    }
}

} // namespace common
