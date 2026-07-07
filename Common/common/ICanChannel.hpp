#pragma once

#include "CanFrame.hpp"

#include <vector>

class ICanChannel {
public:
    virtual ~ICanChannel() = default;

    virtual bool send(const CanFrame& frame) = 0;
    virtual bool send(const std::vector<CanFrame>& frames) = 0;

    virtual bool receive(CanFrame& frame, unsigned long timeout) = 0;
    virtual bool receive(std::vector<CanFrame>& frames, unsigned long timeout) = 0;

    virtual void clearRx() = 0;
    virtual void clearTx() = 0;

    virtual bool startPeriodicMsg(const CanFrame& frame,
                                  unsigned long intervalMs,
                                  unsigned long& msgId) = 0;
    virtual bool stopPeriodicMsg(unsigned long msgId) = 0;

    virtual unsigned long getBaudrate() const = 0;

    virtual bool startMsgFilter(unsigned long filterType,
                                const CanFrame& mask,
                                const CanFrame& pattern,
                                const CanFrame* flowControl,
                                unsigned long& filterId) = 0;
    virtual bool stopMsgFilter(unsigned long filterId) = 0;

    virtual bool setConfig(unsigned long parameter,
                           unsigned long value) = 0;
    virtual bool ioctl(unsigned long ioctlId,
                       const void* input,
                       void* output) = 0;
};
