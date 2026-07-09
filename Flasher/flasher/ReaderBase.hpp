#pragma once

#include "FlasherCallbackHolder.hpp"
#include "FlasherState.hpp"
#include "ParamsTypes.hpp"

#include <common/J2534ChannelProvider.hpp>

#include <functional>
#include <memory>
#include <mutex>
#include <thread>
#include <vector>

class ICanChannel;

namespace j2534 {
class J2534;
}

namespace flasher {

class ReaderBase: public FlasherCallbackHolder {
public:
    ReaderBase(j2534::J2534& j2534, common::CarPlatform carPlatform, uint32_t ecuId, ReadRanges ranges);
    virtual ~ReaderBase();

    FlasherState getCurrentState() const;
    size_t getCurrentProgress() const;
    size_t getMaximumProgress() const;

    const std::vector<std::vector<uint8_t>>& buffers() const { return _buffers; }

    void start();

protected:
    virtual void startImpl(std::vector<std::unique_ptr<ICanChannel>>& channels) = 0;

    void setCurrentState(FlasherState state);
    void incCurrentProgress(size_t delta);
    void setMaximumProgress(size_t maxProgress);
    void setBuffers(std::vector<std::vector<uint8_t>>&& data) { _buffers = std::move(data); }
    void appendToBuffer(size_t index, const std::vector<uint8_t>& data) {
        _buffers[index].insert(_buffers[index].end(), data.begin(), data.end());
    }

    const common::CarPlatform _carPlatform;
    const uint32_t _ecuId;
    const ReadRanges _ranges;

    common::J2534ChannelProvider _channelProvider;
    std::vector<std::vector<uint8_t>> _buffers;

private:
    mutable std::mutex _mutex;
    size_t _currentProgress = 0;
    size_t _maximumProgress = 0;
    FlasherState _currentState = FlasherState::Initial;
    std::thread _readerThread;
};

} // namespace flasher
