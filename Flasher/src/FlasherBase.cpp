#include "flasher/FlasherBase.hpp"

#include <common/ICanChannel.hpp>
#include <j2534/J2534.hpp>
#include <j2534/J2534Channel.hpp>

#define LOG_MODULE_NAME "flasher"
#include <common/LogHelper.hpp>

#include <numeric>

namespace flasher {

size_t FlasherBase::getProgressFromVBF(const common::VBF& vbf)
{
    return std::accumulate(vbf.chunks.cbegin(), vbf.chunks.cend(), size_t{ 0 },
                           [](const auto& value, const auto& chunk) {
                               return value + chunk.data.size();
                           });
}

FlasherBase::FlasherBase(j2534::J2534 &j2534, common::CarPlatform carPlatform, uint32_t ecuId)
    : _j2534ChannelProvider{ j2534, carPlatform }
    , _carPlatform{ carPlatform }
    , _ecuId{ ecuId }
    , _currentProgress{ 0 }
    , _maximumProgress{ 0 }
    , _currentState{ FlasherState::Initial }
    , _flasherThread{}
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

void FlasherBase::start()
{
    _flasherThread = std::thread([this]() {
        LOG_SCOPE_DURATION(FlasherBase_start);
        try {
            auto channels{_j2534ChannelProvider.getAllChannels(_ecuId)};
            startImpl(channels);
        }
        catch(...) {
            setCurrentState(FlasherState::Error);
        }
    });
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
    _currentProgress = std::min(currentProgress, _maximumProgress);
}

void FlasherBase::incCurrentProgress(size_t delta)
{
    std::unique_lock<std::mutex> lock(_mutex);
    _currentProgress = std::min(_currentProgress + delta, _maximumProgress);
}

void FlasherBase::setMaximumProgress(size_t maximumProgress)
{
    std::unique_lock<std::mutex> lock(_mutex);
    _maximumProgress = maximumProgress;
    _currentProgress = std::min(_currentProgress, _maximumProgress);
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
        catch(const std::exception& ex) {
            LOG_MODULE(ERROR) << "Exception during flashing, what = " << ex.what();
            setCurrentState(FlasherState::Error);
        }
        catch(...) {
            LOG_MODULE(ERROR) << "Exception during flashing";
            setCurrentState(FlasherState::Error);
        }
    });
}

} // namespace flasher
