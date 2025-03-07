#include "flasher/FlasherBase.hpp"

#include "flasher/FlasherCallback.hpp"

#include <j2534/J2534.hpp>
#include <j2534/J2534Channel.hpp>

namespace flasher {

FlasherBase::FlasherBase(j2534::J2534 &j2534)
    : _j2534{ j2534 }
    , _currentProgress{ 0 }
    , _maximumProgress{ 0 }
    , _currentState{ FlasherState::Initial }
    , _stopRequested{ false }
{
}

FlasherBase::~FlasherBase()
{
    if(_flasherThread.joinable()) {
        _flasherThread.join();
    }
}

FlasherState FlasherBase::getCurrentState() const
{
    std::unique_lock<std::mutex> lock(_mutex);
    return _currentState;
}

size_t FlasherBase::getCurrentProgress() const
{
    std::unique_lock<std::mutex> lock(_mutex);
    return _currentProgress;
}

size_t FlasherBase::getMaximumProgress() const
{
    std::unique_lock<std::mutex> lock(_mutex);
    return _maximumProgress;
}

void FlasherBase::registerCallback(FlasherCallback &callback)
{
    std::unique_lock<std::mutex> lock{_mutex};
    _callbacks.push_back(&callback);
}

void FlasherBase::unregisterCallback(FlasherCallback &callback)
{
    std::unique_lock<std::mutex> lock{_mutex};
    _callbacks.erase(std::remove(_callbacks.begin(), _callbacks.end(), &callback),
                     _callbacks.end());
}

void FlasherBase::start()
{
    _flasherThread = std::thread([this]() {
        try {
            startImpl();
        }
        catch(...) {
            setCurrentState(FlasherState::Error);
        }
    });
}

j2534::J2534& FlasherBase::getJ2534() const
{
    return _j2534;
}

void FlasherBase::setCurrentState(FlasherState state)
{
    {
        std::unique_lock<std::mutex> lock(_mutex);
        _currentState = state;
    }

    for(const auto& callback: getCallbacks()) {
        callback->OnState(state);
    }
}

void FlasherBase::setCurrentProgress(size_t currentProgress)
{
    std::unique_lock<std::mutex> lock(_mutex);
    _currentProgress = currentProgress;
}

void FlasherBase::setMaximumProgress(size_t maximumProgress)
{
    std::unique_lock<std::mutex> lock(_mutex);
    _maximumProgress = maximumProgress;
}

void FlasherBase::runOnThread(std::function<void()> callable)
{
    if(getCurrentState() != FlasherState::Initial) {
        throw std::runtime_error("Flasher not in initial state");
    }

    _flasherThread = std::thread([this, callable]() {
        try {
            callable();
        }
        catch(...) {
            setCurrentState(FlasherState::Error);
        }
    });
}

std::vector<FlasherCallback *> FlasherBase::getCallbacks() const
{
    std::unique_lock<std::mutex> lock(_mutex);
    return _callbacks;
}

} // namespace flasher
