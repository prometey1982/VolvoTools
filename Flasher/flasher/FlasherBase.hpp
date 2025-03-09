#pragma once

#include "FlasherState.hpp"
#include "SBLProviderBase.hpp"
#include "common/J2534Info.hpp"

#include <functional>
#include <mutex>
#include <vector>

namespace flasher {

class FlasherCallback;

struct FlasherParameters {
    common::CarPlatform carPlatform;
    uint32_t ecuId;
    std::string additionalData;
    std::unique_ptr<SBLProviderBase> sblProvider;
};

class FlasherBase {
public:
    FlasherBase(common::J2534Info &j2534Info, FlasherParameters&& flasherParameters);
    virtual ~FlasherBase();

    FlasherState getCurrentState() const;
    size_t getCurrentProgress() const;
    size_t getMaximumProgress() const;

    void registerCallback(FlasherCallback &callback);
    void unregisterCallback(FlasherCallback &callback);

    void start();

protected:
    virtual void startImpl() = 0;

    common::J2534Info& getJ2534Info() const;
    const FlasherParameters& getFlasherParameters() const;

    void setCurrentState(FlasherState state);
    void setCurrentProgress(size_t currentProgress);
    void setMaximumProgress(size_t maximumProgress);

    void runOnThread(std::function<void()> callable);

private:
    std::vector<FlasherCallback *> getCallbacks() const;

private:
    common::J2534Info &_j2534Info;
    FlasherParameters _flasherParameters;
    mutable std::mutex _mutex;
    size_t _currentProgress;
    size_t _maximumProgress;
    FlasherState _currentState;

    std::thread _flasherThread;

    std::vector<FlasherCallback *> _callbacks;

    bool _stopRequested;
};

} // namespace flasher
