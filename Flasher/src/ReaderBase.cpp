#include "flasher/ReaderBase.hpp"

#include <common/ICanChannel.hpp>
#include <j2534/J2534.hpp>
#include <j2534/J2534Channel.hpp>

#include <numeric>

namespace flasher {

ReaderBase::ReaderBase(j2534::J2534& j2534, common::CarPlatform carPlatform, uint32_t ecuId, ReadRanges ranges)
    : _channelProvider{ j2534, carPlatform }
    , _carPlatform{ carPlatform }
    , _ecuId{ ecuId }
    , _ranges{ ranges }
    , _buffers{ _ranges.size() }
{
    const size_t maximumProgress = std::accumulate(cbegin(ranges), cend(ranges), 0, [](size_t sum, const ReadRange& range) {
        return sum + range.size;
    });
    setMaximumProgress(maximumProgress);
}

ReaderBase::~ReaderBase()
{
    if (_readerThread.joinable()) {
        _readerThread.join();
    }
}

FlasherState ReaderBase::getCurrentState() const
{
    std::unique_lock<std::mutex> lock(_mutex);
    return _currentState;
}

size_t ReaderBase::getCurrentProgress() const
{
    std::unique_lock<std::mutex> lock(_mutex);
    return _currentProgress;
}

size_t ReaderBase::getMaximumProgress() const
{
    std::unique_lock<std::mutex> lock(_mutex);
    return _maximumProgress;
}

void ReaderBase::start()
{
    _readerThread = std::thread([this]() {
        try {
            auto channels{ _channelProvider.getAllChannels(_ecuId) };
            startImpl(channels);
        }
        catch (...) {
            setCurrentState(FlasherState::Error);
        }
    });
}

void ReaderBase::setCurrentState(FlasherState state)
{
    {
        std::unique_lock<std::mutex> lock(_mutex);
        _currentState = state;
    }
    for (const auto& callback : getCallbacks()) {
        callback->OnState(state);
    }
}

void ReaderBase::incCurrentProgress(size_t delta)
{
    std::unique_lock<std::mutex> lock(_mutex);
    _currentProgress = std::min(_currentProgress + delta, _maximumProgress);
}

void ReaderBase::setMaximumProgress(size_t maxProgress)
{
    std::unique_lock<std::mutex> lock(_mutex);
    _maximumProgress = maxProgress;
    _currentProgress = std::min(_currentProgress, _maximumProgress);
}

} // namespace flasher
