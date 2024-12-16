#include "flasher/UDSFlasher.hpp"

#include <j2534/J2534.hpp>
#include <j2534/J2534Channel.hpp>

#include <common/CanMessage.hpp>
#include <common/Util.hpp>

#include <unordered_map>

namespace flasher {

namespace {
j2534::J2534Channel& getChannel(uint32_t cmId, const std::vector<std::unique_ptr<j2534::J2534Channel>>& channels)
{
    static const std::unordered_map<uint32_t, size_t> CMMap = {
        {0x7E0, 0}
    };
    return *channels[CMMap.at(cmId)];
}
}

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
        _channels.emplace_back(common::openChannel(_j2534, CAN, CAN_ID_BOTH, 500000,
            true));
        _channels.emplace_back(common::openLowSpeedChannel(_j2534, CAN_ID_BOTH));
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
    FallingAsleepStep(const std::vector<std::unique_ptr<j2534::J2534Channel>>& channels)
        : UDSStep{ FlasherStep::FallingAsleep, 100, true }
        , _channels{ channels }
    {
    }

    bool processImpl() override
    {
        std::vector<unsigned long> msg_id(_channels.size());
        for(size_t i = 0; i < _channels.size(); ++i) {
            PASSTHRU_MSG msg;

            memset(&msg, 0, sizeof(msg));
            msg.ProtocolID = _channels[i]->getProtocolId();
            msg.DataSize = 12;
            msg.Data[2] = 0x7;
            msg.Data[3] = 0xE0;// 0xDF;
            msg.Data[4] = 0x02;
            msg.Data[5] = 0x10;
            msg.Data[6] = 0x02;
            if (_channels[i]->startPeriodicMsg(msg, msg_id[i], 5) != STATUS_NOERROR)
            {
                return false;
            }
        }
        std::this_thread::sleep_for(std::chrono::seconds(4));
        for (size_t i = 0; i < _channels.size(); ++i) {
            _channels[i]->stopPeriodicMsg(msg_id[i]);
        }
        return true;
    }

private:
    const std::vector<std::unique_ptr<j2534::J2534Channel>>& _channels;
};

class KeepAliveStep : public UDSStep {
public:
    KeepAliveStep(const std::vector<std::unique_ptr<j2534::J2534Channel>>& channels, uint32_t cmId)
        : UDSStep{ FlasherStep::FallingAsleep, 100, true }
        , _channels{ channels }
        , _cmId{ cmId }
    {
    }

    bool processImpl() override
    {
        const auto& channel = getChannel(_cmId, _channels);
        PASSTHRU_MSG msg;
        memset(&msg, 0, sizeof(msg));
        msg.ProtocolID = channel.getProtocolId();
        msg.DataSize = 12;
        msg.Data[2] = 0x7;
        msg.Data[3] = 0xDF;
        msg.Data[4] = 0x02;
        msg.Data[5] = 0x3E;
        msg.Data[6] = 0x80;
        unsigned long msg_id;
        if (channel.startPeriodicMsg(msg, msg_id, 1900) != STATUS_NOERROR)
        {
            return false;
        }
        return true;
    }

private:
    const std::vector<std::unique_ptr<j2534::J2534Channel>>& _channels;
    uint32_t _cmId;
};

class WakeUpStep : public UDSStep {
public:
    WakeUpStep(const std::vector<std::unique_ptr<j2534::J2534Channel>>& channels)
        : UDSStep{ FlasherStep::WakeUp, 100, false }
        , _channels{ channels }
    {
    }

    bool processImpl() override
    {
        return true;
    }

private:
    const std::vector<std::unique_ptr<j2534::J2534Channel>>& _channels;
};

class AuthorizingStep : public UDSStep {
public:
    AuthorizingStep(const std::vector<std::unique_ptr<j2534::J2534Channel>>& channels,
        uint32_t cmId, const std::array<uint8_t, 5>& pin)
        : UDSStep{ FlasherStep::Authorizing, 100, true }
        , _channels{ channels }
        , _cmId{ cmId }
        , _pin{ pin }
    {
    }

    bool processImpl() override
    {
        const auto& channel = getChannel(_cmId, _channels);
        channel.clearRx();
        common::CanMessage requestSeedMsg(_cmId, { 0x02, 0x27, 0x01 });
        unsigned long numMsgs;
        if (channel.writeMsgs(requestSeedMsg, numMsgs) != STATUS_NOERROR || numMsgs < 1) {
            return false;
        }
        std::vector<PASSTHRU_MSG> read_msgs;
        read_msgs.resize(1);
        if (channel.readMsgs(read_msgs, 5000) != STATUS_NOERROR || read_msgs.empty())
        {
            return false;
        }
        std::array<uint8_t, 3> seed = { read_msgs[0].Data[7], read_msgs[0].Data[8], read_msgs[0].Data[9] };
        std::array<uint8_t, 3> key;
        generateKey(_pin, seed, key);
        channel.clearRx();
        common::CanMessage sendKeyMsg(_cmId, { 0x05, 0x27, 0x02, key[0], key[1], key[2] });
        if (channel.writeMsgs(sendKeyMsg, numMsgs) != STATUS_NOERROR || numMsgs < 1) {
            return false;
        }
        read_msgs.resize(1);
        if (channel.readMsgs(read_msgs) != STATUS_NOERROR || read_msgs.empty())
        {
            return false;
        }
        const auto& answer_data = read_msgs[0].Data;
        if (answer_data[4] == 2 && answer_data[5] == 0x67 && answer_data[6] == 2)
        {
            return true;
        }
        return false;
    }

private:
    uint32_t generateKey(const std::array<uint8_t, 5>& pin_array, const std::array<uint8_t, 3>& seed_array,
                         std::array <uint8_t, 3>& key_array)
    {
        unsigned int high_part;
        unsigned int low_part;
        unsigned int hash = 0xC541A9;
        uint8_t old;
        uint8_t is_bit_set;
        uint32_t result;

        high_part = pin_array[4] << 24 | pin_array[3] << 16 | pin_array[2] << 8 | pin_array[1];
        low_part = pin_array[0] << 24 | seed_array[2] << 16 | seed_array[1] << 8 | seed_array[0];
        for (size_t i = 0; i < 32; ++i)
        {
            old = low_part;
            low_part >>= 1;
            is_bit_set = hash ^ old;
            hash >>= 1;
            if ((is_bit_set & 1) != 0)
                hash = (hash | 0x800000) ^ 0x109028;
        }
        for (size_t i = 0; i < 32; ++i)
        {
            old = high_part;
            high_part >>= 1;
            is_bit_set = hash ^ old;
            hash >>= 1;
            if ((is_bit_set & 1) != 0)
                hash = (hash | 0x800000) ^ 0x109028;
        }
        result = ((hash & 0xF00000) >> 12) | hash & 0xF000 | (uint8_t)(16 * hash) | ((hash & 0xFF0) << 12) | ((hash & 0xF0000) >> 16);
        key_array[0] = (result >> 16) & 0xFF;
        key_array[1] = (result >> 8) & 0xFF;
        key_array[2] = result & 0xFF;
        return result;
    }

    const std::vector<std::unique_ptr<j2534::J2534Channel>>& _channels;
    uint32_t _cmId;
    const std::array<uint8_t, 5>& _pin;
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
    DataTransferStep(FlasherStep step, const std::vector<std::unique_ptr<j2534::J2534Channel>>& channels, uint32_t cmId, const VBF& data)
        : UDSStep{ step, getMaximumSize(data), true }
        , _channels{ channels }
        , _cmId{ cmId }
        , _data{ data }
    {
    }

    bool processImpl() override
    {
        return true;
    }

private:
    const std::vector<std::unique_ptr<j2534::J2534Channel>>& _channels;
    uint32_t _cmId;
    const VBF& _data;
};

class BootloaderActivatingStep : public UDSStep {
public:
    BootloaderActivatingStep(const std::vector<std::unique_ptr<j2534::J2534Channel>>& channels,
        uint32_t cmId, const VBF& bootloader)
        : UDSStep{ FlasherStep::BootloaderLoading, 100, true }
        , _channels{ channels }
        , _cmId{ cmId }
        , _startAddress{ bootloader.jumpAddr }
    {
    }

    bool processImpl() override
    {
        return true;
    }

private:
    const std::vector<std::unique_ptr<j2534::J2534Channel>>& _channels;
    uint32_t _cmId;
    uint32_t _startAddress;
};

class FlashErasingStep : public UDSStep {
public:
    FlashErasingStep(const std::vector<std::unique_ptr<j2534::J2534Channel>>& channels, uint32_t cmId, const VBF& flash)
        : UDSStep{ FlasherStep::FlashErasing, 100 * flash.chunks.size(), true}
        , _channels{ channels }
        , _cmId{ cmId }
        , _flash{ flash }
    {
    }

    bool processImpl() override
    {
        return true;
    }

private:
    const std::vector<std::unique_ptr<j2534::J2534Channel>>& _channels;
    uint32_t _cmId;
    const VBF& _flash;
};

UDSFlasher::UDSFlasher(j2534::J2534 &j2534, uint32_t cmId, const std::array<uint8_t, 5>& pin, const VBF& bootloader, const VBF& flash)
	: _j2534{ j2534 }
	, _cmId{ cmId }
	, _pin{ pin }
	, _bootloader{ bootloader }
	, _flash{ flash }
    , _currentState{ State::Initial }
{
    _steps.emplace_back(new OpenChannelsStep(_j2534, _channels));
    _steps.emplace_back(new FallingAsleepStep(_channels));
    _steps.emplace_back(new KeepAliveStep(_channels, _cmId));
    _steps.emplace_back(new AuthorizingStep(_channels, _cmId, _pin));
    _steps.emplace_back(new DataTransferStep(FlasherStep::BootloaderLoading, _channels, _cmId, _bootloader));
    _steps.emplace_back(new BootloaderActivatingStep(_channels, _cmId, _bootloader));
    _steps.emplace_back(new FlashErasingStep(_channels, _cmId, _flash));
    _steps.emplace_back(new DataTransferStep(FlasherStep::FlashLoading, _channels, _cmId, _flash));
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
        bool currentStepFailed = !step->process(failed);
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
