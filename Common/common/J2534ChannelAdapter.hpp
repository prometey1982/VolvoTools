#pragma once

#include "ICanChannel.hpp"

#include <memory>

namespace j2534 {
    class J2534Channel;
}

class J2534ChannelAdapter final : public ICanChannel {
public:
    explicit J2534ChannelAdapter(std::unique_ptr<j2534::J2534Channel> channel);
    ~J2534ChannelAdapter() override;

    bool send(const CanFrame& frame) override;
    bool send(const std::vector<CanFrame>& frames) override;

    bool receive(CanFrame& frame, unsigned long timeout) override;
    bool receive(std::vector<CanFrame>& frames, unsigned long timeout) override;

    void clearRx() override;
    void clearTx() override;

    bool startPeriodicMsg(const CanFrame& frame,
                          unsigned long intervalMs,
                          unsigned long& msgId) override;
    bool stopPeriodicMsg(unsigned long msgId) override;

    bool startMsgFilter(unsigned long filterType,
                        const CanFrame& mask,
                        const CanFrame& pattern,
                        const CanFrame* flowControl,
                        unsigned long& filterId) override;
    bool stopMsgFilter(unsigned long filterId) override;

    bool setConfig(unsigned long parameter,
                   unsigned long value) override;
    bool ioctl(unsigned long ioctlId,
               const void* input,
               void* output) override;

    unsigned long getBaudrate() const override;

private:
    std::unique_ptr<j2534::J2534Channel> _channel;
    unsigned long _protocolId;
    unsigned long _txFlags;
};
