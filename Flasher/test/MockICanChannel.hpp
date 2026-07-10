#pragma once

#include <common/ICanChannel.hpp>

#include <cstdint>
#include <queue>
#include <vector>

using namespace common;

class MockICanChannel final : public ICanChannel {
public:
    bool send(const CanFrame&, unsigned long = 1000) override {
        ++sendCount;
        return !failOnSend;
    }

    bool send(const std::vector<CanFrame>& frames, unsigned long = 1000) override {
        sendCount += static_cast<int>(frames.size());
        return !failOnSend;
    }

    bool receive(CanFrame& frame, unsigned long) override {
        if (receiveQueue.empty()) return false;
        frame = receiveQueue.front();
        receiveQueue.pop();
        return true;
    }
    bool receive(std::vector<CanFrame>& frames, unsigned long) override {
        if (receiveQueue.empty()) return false;
        while (!receiveQueue.empty()) {
            frames.push_back(receiveQueue.front());
            receiveQueue.pop();
        }
        return true;
    }

    void clearRx() override {}
    void clearTx() override {}

    bool startPeriodicMsg(const CanFrame&, unsigned long, unsigned long& msgId) override {
        msgId = ++nextMsgId;
        return !failOnPeriodic;
    }

    bool stopPeriodicMsg(unsigned long) override { return true; }

    bool startMsgFilter(unsigned long, const CanFrame&, const CanFrame&,
                         const CanFrame*, unsigned long&) override { return true; }
    bool stopMsgFilter(unsigned long) override { return true; }
    bool setConfig(unsigned long, unsigned long) override { return true; }
    bool ioctl(unsigned long, const void*, void*) override { return true; }
    unsigned long getBaudrate() const override { return 500000; }

    int sendCount = 0;
    bool failOnSend = false;
    bool failOnPeriodic = false;
    std::queue<CanFrame> receiveQueue;
    int nextMsgId = 0;
};

// Wraps a reference to MockICanChannel as a unique_ptr<ICanChannel>
class MockChannelWrapper : public ICanChannel {
public:
    explicit MockChannelWrapper(MockICanChannel& mock) : _mock{ mock } {}

    bool send(const CanFrame& frame, unsigned long timeout = 1000) override { return _mock.send(frame, timeout); }
    bool send(const std::vector<CanFrame>& frames, unsigned long timeout = 1000) override { return _mock.send(frames, timeout); }
    bool receive(CanFrame& frame, unsigned long timeout) override { return _mock.receive(frame, timeout); }
    bool receive(std::vector<CanFrame>& frames, unsigned long timeout) override { return _mock.receive(frames, timeout); }
    void clearRx() override { _mock.clearRx(); }
    void clearTx() override { _mock.clearTx(); }
    bool startPeriodicMsg(const CanFrame& frame, unsigned long interval, unsigned long& msgId) override {
        return _mock.startPeriodicMsg(frame, interval, msgId);
    }
    bool stopPeriodicMsg(unsigned long msgId) override { return _mock.stopPeriodicMsg(msgId); }
    bool startMsgFilter(unsigned long type, const CanFrame& mask, const CanFrame& pattern,
                         const CanFrame* flow, unsigned long& filterId) override {
        return _mock.startMsgFilter(type, mask, pattern, flow, filterId);
    }
    bool stopMsgFilter(unsigned long filterId) override { return _mock.stopMsgFilter(filterId); }
    bool setConfig(unsigned long param, unsigned long value) override { return _mock.setConfig(param, value); }
    bool ioctl(unsigned long id, const void* in, void* out) override { return _mock.ioctl(id, in, out); }
    unsigned long getBaudrate() const override { return _mock.getBaudrate(); }

private:
    MockICanChannel& _mock;
};
