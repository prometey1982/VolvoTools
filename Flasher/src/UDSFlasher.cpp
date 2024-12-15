#include "flasher/UDSFlasher.hpp"

#include <j2534/J2534.hpp>
#include <j2534/J2534Channel.hpp>

#include <mutex>

namespace flasher {

class UDSStep {
public:
    UDSStep(FlasherStep step, size_t maximumProgress, bool skipOnError)
        : _step{ step }
        , _currentProgress{}
        , _maximumProgress{ maximumProgress }
        , _skipOnError{ skipOnError }
    {
    }

    bool process(bool previousFailed)
    {
        bool result = true;
        if (!(previousFailed && _skipOnError)) {
            try {
                result = processImpl();
            }
            catch (...) {
                result = false;
            }
        }
        setCurrentProgress(getMaximumProgress());
        return result;
    }

    size_t getCurrentProgress() const
    {
        std::unique_lock<std::mutex> lock(_mutex);
        return _currentProgress;
    }

    size_t getMaximumProgress() const
    {
        std::unique_lock<std::mutex> lock(_mutex);
        return _maximumProgress;
    }

    FlasherStep getStep() const
    {
        return _step;
    }

protected:
    virtual bool processImpl() = 0;

    void setCurrentProgress(size_t currentProgress)
    {
        std::unique_lock<std::mutex> lock(_mutex);
        _currentProgress = currentProgress;
    }

    void setMaximumProgress(size_t maximumProgress)
    {
        std::unique_lock<std::mutex> lock(_mutex);
        _maximumProgress = maximumProgress;
    }

private:
    FlasherStep _step;
    mutable std::mutex _mutex;

    size_t _currentProgress;
    size_t _maximumProgress;

    bool _skipOnError;
};

class OpenChannelsStep : public UDSStep {
public:
    OpenChannelsStep(j2534::J2534& j2534, std::vector<std::unique_ptr<j2534::J2534Channel>>& channels)
        : UDSStep{ FlasherStep::OpenChannels, 100, true }
        , _j2534{ j2534 }
        , _channels{ channels }
    {
    }

    bool processImpl() override
    {
        return true;
    }

private:
    j2534::J2534& _j2534;
    std::vector<std::unique_ptr<j2534::J2534Channel>>& _channels;
};

class CloseChannelsStep : public UDSStep {
public:
    CloseChannelsStep(std::vector<std::unique_ptr<j2534::J2534Channel>>& channels)
        : UDSStep{ FlasherStep::CloseChannels, 100, false }
        , _channels{ channels }
    {
    }

    bool processImpl() override
    {
        _channels.clear();
        return true;
    }

private:
    std::vector<std::unique_ptr<j2534::J2534Channel>>& _channels;
};

class FallingAsleepStep : public UDSStep {
public:
    FallingAsleepStep(std::vector<std::unique_ptr<j2534::J2534Channel>>& channels)
        : UDSStep{ FlasherStep::FallingAsleep, 100, true }
        , _channels{ channels }
    {
    }

    bool processImpl() override
    {
        return true;
    }

private:
    std::vector<std::unique_ptr<j2534::J2534Channel>>& _channels;
};

class WakeUpStep : public UDSStep {
public:
    WakeUpStep(std::vector<std::unique_ptr<j2534::J2534Channel>>& channels)
        : UDSStep{ FlasherStep::WakeUp, 100, false }
        , _channels{ channels }
    {
    }

    bool processImpl() override
    {
        return true;
    }

private:
    std::vector<std::unique_ptr<j2534::J2534Channel>>& _channels;
};

class AuthorizingStep : public UDSStep {
public:
    AuthorizingStep(j2534::J2534Channel& channel, uint32_t cmId, const std::vector<uint8_t>& pin)
        : UDSStep{ FlasherStep::Authorizing, 100, true }
        , _channel{ channel }
        , _cmId{ cmId }
        , _pin{ pin }
    {
    }

    bool processImpl() override
    {
        return true;
    }

private:
    j2534::J2534Channel& _channel;
    uint32_t _cmId;
    const std::vector<uint8_t>& _pin;
};

class DataTransferStep : public UDSStep {
    static size_t getMaximumSize(const VBF& data)
    {
        size_t result = 0;
        for (const auto chunk : data.chunks) {
            result += chunk.data.size();
        }
        return result;
    }
public:
    DataTransferStep(FlasherStep step, j2534::J2534Channel& channel, uint32_t cmId, const VBF& data)
        : UDSStep{ step, getMaximumSize(data), true }
        , _channel{ channel }
        , _cmId{ cmId }
        , _data{ data }
    {
    }

    bool processImpl() override
    {
        return true;
    }

private:
    j2534::J2534Channel& _channel;
    uint32_t _cmId;
    const VBF& _data;
};

class BootloaderActivatingStep : public UDSStep {
public:
    BootloaderActivatingStep(j2534::J2534Channel& channel, uint32_t cmId, const VBF& bootloader)
        : UDSStep{ FlasherStep::BootloaderLoading, 100, true }
        , _channel{ channel }
        , _cmId{ cmId }
        , _startAddress{ bootloader.jumpAddr }
    {
    }

    bool processImpl() override
    {
        return true;
    }

private:
    j2534::J2534Channel& _channel;
    uint32_t _cmId;
    uint32_t _startAddress;
};

class FlashErasingStep : public UDSStep {
public:
    FlashErasingStep(j2534::J2534Channel& channel, uint32_t cmId, const VBF& flash)
        : UDSStep{ FlasherStep::FlashErasing, 100 * flash.chunks.size(), true}
        , _channel{ channel }
        , _cmId{ cmId }
        , _flash{ flash }
    {
    }

    bool processImpl() override
    {
        return true;
    }

private:
    j2534::J2534Channel& _channel;
    uint32_t _cmId;
    const VBF& _flash;
};

UDSFlasher::UDSFlasher(j2534::J2534 &j2534, uint32_t cmId, const std::vector<uint8_t>& pin, const VBF& bootloader, const VBF& flash)
	: _j2534{ j2534 }
	, _cmId{ cmId }
	, _pin{ pin }
	, _bootloader{ bootloader }
	, _flash{ flash }
    , _currentState{ State::Initial }
{
    _steps.emplace_back(new OpenChannelsStep(_j2534, _channels));
    _steps.emplace_back(new FallingAsleepStep(_channels));
    _steps.emplace_back(new AuthorizingStep(*_channels[0], _cmId, _pin));
    _steps.emplace_back(new DataTransferStep(FlasherStep::BootloaderLoading, *_channels[0], _cmId, _bootloader));
    _steps.emplace_back(new BootloaderActivatingStep(*_channels[0], _cmId, _bootloader));
    _steps.emplace_back(new FlashErasingStep(*_channels[0], _cmId, _flash));
    _steps.emplace_back(new DataTransferStep(FlasherStep::FlashLoading, *_channels[0], _cmId, _flash));
    _steps.emplace_back(new WakeUpStep(_channels));
    _steps.emplace_back(new CloseChannelsStep(_channels));
}

UDSFlasher::~UDSFlasher()
{
}

void UDSFlasher::flash()
{
    setState(State::InProgress);
    bool failed = false;
    for (const auto& step : _steps) {
        bool currentStepFailed = step->process(failed);
        if (!failed && currentStepFailed) {
            setState(State::Error);
        }
        failed |= currentStepFailed;
        size_t currentMaximumProgress = step->getMaximumProgress();
    }
    setState(State::Done);
}

UDSFlasher::State UDSFlasher::getState() const
{
    std::unique_lock<std::mutex> lock(_mutex);
    return _currentState;
}

size_t UDSFlasher::getCurrentProgress() const
{
    size_t result = 0;
    for (const auto& step : _steps) {
        result += step->getCurrentProgress();
    }
    return result;
}

size_t UDSFlasher::getMaximumProgress() const
{
    size_t result = 0;
    for (const auto& step : _steps) {
        result += step->getMaximumProgress();
    }
    return result;
}

void UDSFlasher::setState(State newState)
{
    std::unique_lock<std::mutex> lock(_mutex);
    _currentState = newState;
}

} // namespace flasher
