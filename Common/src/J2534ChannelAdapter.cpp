#include "common/J2534ChannelAdapter.hpp"

#include <j2534/J2534Channel.hpp>
#include <j2534/J2534_v0404.h>

#define LOG_MODULE_NAME "common"
#include "common/LogHelper.hpp"

#include <cstring>

namespace {

void canFrameToPassthruMsg(const CanFrame& frame,
                            unsigned long protocolId,
                            unsigned long txFlags,
                            PASSTHRU_MSG& msg) {
    std::memset(&msg, 0, sizeof(msg));
    msg.ProtocolID = protocolId;
    msg.RxStatus = 0;
    msg.TxFlags = txFlags | (frame.isExtendedId ? CAN_29BIT_ID : 0);
    msg.Timestamp = 0;
    msg.ExtraDataIndex = 0;
    msg.DataSize = static_cast<unsigned long>(frame.data.size() + 4);
    msg.Data[0] = (frame.id >> 24) & 0xFF;
    msg.Data[1] = (frame.id >> 16) & 0xFF;
    msg.Data[2] = (frame.id >> 8) & 0xFF;
    msg.Data[3] = frame.id & 0xFF;
    if (!frame.data.empty()) {
        std::memcpy(msg.Data + 4, frame.data.data(), frame.data.size());
    }
}

CanFrame passthruMsgToCanFrame(const PASSTHRU_MSG& msg) {
    CanFrame frame;
    frame.id = (static_cast<uint32_t>(msg.Data[0]) << 24) |
               (static_cast<uint32_t>(msg.Data[1]) << 16) |
               (static_cast<uint32_t>(msg.Data[2]) << 8) |
               static_cast<uint32_t>(msg.Data[3]);
    frame.isExtendedId = (msg.RxStatus & CAN_29BIT_ID) != 0;
    if (msg.DataSize > 4) {
        frame.data.assign(msg.Data + 4, msg.Data + msg.DataSize);
    }
    return frame;
}

} // anonymous namespace

J2534ChannelAdapter::J2534ChannelAdapter(std::unique_ptr<j2534::J2534Channel> channel)
    : _channel{ std::move(channel) }
    , _protocolId{ _channel->getProtocolId() }
    , _txFlags{ _channel->getTxFlags() }
{
}

unsigned long J2534ChannelAdapter::getBaudrate() const
{
    return _channel->getBaudrate();
}

J2534ChannelAdapter::~J2534ChannelAdapter() = default;

bool J2534ChannelAdapter::send(const CanFrame& frame, unsigned long timeout) {
    PASSTHRU_MSG msg;
    canFrameToPassthruMsg(frame, _protocolId, _txFlags, msg);
    unsigned long numMsgs = 1;
    auto rc = _channel->writeMsgs({ msg }, numMsgs, timeout);
    if (rc != STATUS_NOERROR) {
        LOG_MODULE(DEBUG) << "send failed, rc=" << rc;
    }
    return rc == STATUS_NOERROR;
}

bool J2534ChannelAdapter::send(const std::vector<CanFrame>& frames, unsigned long timeout) {
    std::vector<PASSTHRU_MSG> msgs;
    msgs.reserve(frames.size());
    for (const auto& frame : frames) {
        PASSTHRU_MSG msg;
        canFrameToPassthruMsg(frame, _protocolId, _txFlags, msg);
        msgs.push_back(msg);
    }
    unsigned long numMsgs = static_cast<unsigned long>(msgs.size());
    auto rc = _channel->writeMsgs(msgs, numMsgs, timeout);
    if (rc != STATUS_NOERROR) {
        LOG_MODULE(DEBUG) << "send (batch) failed, rc=" << rc;
    }
    return rc == STATUS_NOERROR;
}

bool J2534ChannelAdapter::receive(CanFrame& frame, unsigned long timeout) {
    std::vector<PASSTHRU_MSG> msgs(1);
    auto rc = _channel->readMsgs(msgs, timeout);
    if (rc != STATUS_NOERROR || msgs.empty()) {
        if (rc != STATUS_NOERROR) {
            LOG_MODULE(DEBUG) << "receive failed, rc=" << rc;
        }
        return false;
    }
    frame = passthruMsgToCanFrame(msgs[0]);
    return true;
}

bool J2534ChannelAdapter::receive(std::vector<CanFrame>& frames, unsigned long timeout) {
    std::vector<PASSTHRU_MSG> msgs(16);
    auto rc = _channel->readMsgs(msgs, timeout);
    if (rc != STATUS_NOERROR || msgs.empty()) {
        if (rc != STATUS_NOERROR) {
            LOG_MODULE(DEBUG) << "receive (batch) failed, rc=" << rc;
        }
        return false;
    }
    frames.clear();
    frames.reserve(msgs.size());
    for (const auto& msg : msgs) {
        frames.push_back(passthruMsgToCanFrame(msg));
    }
    return true;
}

void J2534ChannelAdapter::clearRx() {
    _channel->clearRx();
}

void J2534ChannelAdapter::clearTx() {
    _channel->clearTx();
}

bool J2534ChannelAdapter::startPeriodicMsg(const CanFrame& frame,
                                            unsigned long intervalMs,
                                            unsigned long& msgId) {
    PASSTHRU_MSG msg;
    canFrameToPassthruMsg(frame, _protocolId, _txFlags, msg);
    return _channel->startPeriodicMsg(msg, msgId, intervalMs) == STATUS_NOERROR;
}

bool J2534ChannelAdapter::stopPeriodicMsg(unsigned long msgId) {
    return _channel->stopPeriodicMsg(msgId) == STATUS_NOERROR;
}

bool J2534ChannelAdapter::startMsgFilter(unsigned long filterType,
                                          const CanFrame& mask,
                                          const CanFrame& pattern,
                                          const CanFrame* flowControl,
                                          unsigned long& filterId) {
    PASSTHRU_MSG maskMsg;
    canFrameToPassthruMsg(mask, _protocolId, _txFlags, maskMsg);
    PASSTHRU_MSG patternMsg;
    canFrameToPassthruMsg(pattern, _protocolId, _txFlags, patternMsg);
    PASSTHRU_MSG flowMsg;
    PASSTHRU_MSG* flowPtr = nullptr;
    if (flowControl) {
        canFrameToPassthruMsg(*flowControl, _protocolId, _txFlags, flowMsg);
        flowPtr = &flowMsg;
    }
    return _channel->startMsgFilter(filterType, &maskMsg, &patternMsg, flowPtr, filterId) == STATUS_NOERROR;
}

bool J2534ChannelAdapter::stopMsgFilter(unsigned long filterId) {
    return _channel->stopMsgFilter(filterId) == STATUS_NOERROR;
}

bool J2534ChannelAdapter::setConfig(unsigned long parameter, unsigned long value) {
    SCONFIG cfg{ parameter, value };
    return _channel->setConfig({ cfg }) == STATUS_NOERROR;
}

bool J2534ChannelAdapter::ioctl(unsigned long ioctlId,
                                 const void* input,
                                 void* output) {
    return _channel->passThruIoctl(ioctlId, input, output) == STATUS_NOERROR;
}
