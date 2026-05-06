#include "flasher/FlasherBase.hpp"

#include "flasher/FlasherCallback.hpp"

#include <j2534/J2534.hpp>
#include <j2534/J2534Channel.hpp>

#include <easylogging++.h>

#include <algorithm>
#include <numeric>

namespace flasher {

size_t FlasherBase::getProgressFromVBF(const common::VBF& vbf)
{
    return std::accumulate(vbf.chunks.cbegin(), vbf.chunks.cend(), size_t{ 0 },
                           [](const auto& value, const auto& chunk) {
                               return value + chunk.data.size();
                           });
}

FlasherBase::FlasherBase(j2534::J2534 &j2534, FlasherParameters&& flasherParameters)
    : _j2534ChannelProvider{ j2534, flasherParameters.carPlatform }
    , _flasherParameters{ std::move(flasherParameters) }
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

std::string FlasherBase::getLastError() const
{
    std::unique_lock<std::mutex> lock(_mutex);
    return _lastError;
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
            LOG(INFO) << "FlasherBase opening channels for ecu=0x" << std::hex
                << _flasherParameters.ecuId;
            auto channels{openChannels()};
            LOG(INFO) << "FlasherBase opened channels count=" << std::dec << channels.size();
            LOG(INFO) << "FlasherBase startImpl enter";
            startImpl(channels);
            LOG(INFO) << "FlasherBase startImpl exit";
        }
        catch(const std::exception& ex) {
            LOG(ERROR) << "Exception during flashing, what = " << ex.what();
            setLastError(ex.what());
            setCurrentState(FlasherState::Error);
        }
        catch(...) {
            LOG(ERROR) << "Exception during flashing";
            setLastError("Unknown exception during flashing");
            setCurrentState(FlasherState::Error);
        }
    });
}

std::vector<std::unique_ptr<j2534::J2534Channel>> FlasherBase::openChannels()
{
    return _j2534ChannelProvider.getAllChannels(_flasherParameters.ecuId);
}

std::unique_ptr<j2534::J2534Channel> FlasherBase::openChannelForEcu(uint32_t ecuId)
{
    return _j2534ChannelProvider.getChannelForEcu(ecuId);
}

const FlasherParameters& FlasherBase::getFlasherParameters() const
{
    return _flasherParameters;
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

void FlasherBase::setLastError(const std::string& error)
{
    std::unique_lock<std::mutex> lock(_mutex);
    _lastError = error;
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
            LOG(ERROR) << "Exception during flashing, what = " << ex.what();
            setLastError(ex.what());
            setCurrentState(FlasherState::Error);
        }
        catch(...) {
            LOG(ERROR) << "Exception during flashing";
            setLastError("Unknown exception during flashing");
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
