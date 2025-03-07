#pragma once

#include "FlasherState.hpp"

#include <functional>
#include <mutex>
#include <vector>

namespace j2534 {
class J2534;
class J2534Channel;
} // namespace j2534

namespace flasher {

class FlasherCallback;

class FlasherBase {
public:
    FlasherBase(j2534::J2534 &j2534);
    virtual ~FlasherBase();

    FlasherState getCurrentState() const;
    size_t getCurrentProgress() const;
    size_t getMaximumProgress() const;

    void registerCallback(FlasherCallback &callback);
    void unregisterCallback(FlasherCallback &callback);

    void start();

protected:
    virtual void startImpl() = 0;

    j2534::J2534& getJ2534() const;

    void setCurrentState(FlasherState state);
    void setCurrentProgress(size_t currentProgress);
    void setMaximumProgress(size_t maximumProgress);

    void runOnThread(std::function<void()> callable);

private:
    std::vector<FlasherCallback *> getCallbacks() const;

protected:
    std::vector<std::unique_ptr<j2534::J2534Channel>> _channels;

private:
    j2534::J2534 &_j2534;
    mutable std::mutex _mutex;
    size_t _currentProgress;
    size_t _maximumProgress;
    FlasherState _currentState;

    std::thread _flasherThread;

    std::vector<FlasherCallback *> _callbacks;

    bool _stopRequested;
};

} // namespace flasher
