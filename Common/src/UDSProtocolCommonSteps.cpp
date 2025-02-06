#include "common/UDSProtocolCommonSteps.hpp"

#include "common/UDSMessage.hpp"
#include "common/Util.hpp"

namespace common {

    OpenChannelsStep::OpenChannelsStep(j2534::J2534& j2534, uint32_t cmId, std::vector<std::unique_ptr<j2534::J2534Channel>>& channels)
        : UDSProtocolStep{ UDSStepType::OpenChannels, 100, true }
        , _j2534{ j2534 }
        , _cmId{ cmId }
        , _channels{ channels }
    {
    }

    bool OpenChannelsStep::processImpl()
    {
        _channels.emplace_back(openUDSChannel(_j2534, 500000, _cmId));
        _channels.emplace_back(openLowSpeedChannel(_j2534, CAN_ID_BOTH));
        return true;
    }

    CloseChannelsStep::CloseChannelsStep(std::vector<std::unique_ptr<j2534::J2534Channel>>& channels)
        : UDSProtocolStep{ UDSStepType::CloseChannels, 100, false }
        , _channels{ channels }
    {
    }

    bool CloseChannelsStep::processImpl()
    {
        _channels.clear();
        return true;
    }

    FallingAsleepStep::FallingAsleepStep(const std::vector<std::unique_ptr<j2534::J2534Channel>>& channels)
        : UDSProtocolStep{ UDSStepType::FallingAsleep, 100, true }
        , _channels{ channels }
    {
    }

    bool FallingAsleepStep::processImpl()
    {
        std::vector<std::vector<unsigned long>> msgIds(_channels.size());
        for (size_t i = 0; i < _channels.size(); ++i) {
            const auto ids = _channels[i]->startPeriodicMsgs(UDSMessage(0x7DF, { 0x10, 0x02 }), 5);
            if (ids.empty()) {
                return false;
            }
            msgIds[i] = ids;
        }
        std::this_thread::sleep_for(std::chrono::seconds(2));
        for (size_t i = 0; i < _channels.size(); ++i) {
            _channels[i]->stopPeriodicMsg(msgIds[i]);
        }
        return true;
    }

    KeepAliveStep::KeepAliveStep(const std::vector<std::unique_ptr<j2534::J2534Channel>>& channels, uint32_t cmId)
        : UDSProtocolStep{ UDSStepType::FallingAsleep, 100, true }
        , _channels{ channels }
        , _cmId{ cmId }
    {
    }

    bool KeepAliveStep::processImpl()
    {
        const auto& channel = getChannelByEcuId(_cmId, _channels);
        if (channel.startPeriodicMsgs(UDSMessage(0x7DF, { 0x3E, 0x80 }), 1900).empty())
        {
            return false;
        }
        return true;
    }

    WakeUpStep::WakeUpStep(const std::vector<std::unique_ptr<j2534::J2534Channel>>& channels)
        : UDSProtocolStep{ UDSStepType::WakeUp, 100, false }
        , _channels{ channels }
    {
    }

    bool WakeUpStep::processImpl()
    {
        return true;
    }

    AuthorizingStep::AuthorizingStep(const std::vector<std::unique_ptr<j2534::J2534Channel>>& channels,
        uint32_t cmId, const std::array<uint8_t, 5>& pin)
        : UDSProtocolStep{ UDSStepType::Authorizing, 100, true }
        , _channels{ channels }
        , _cmId{ cmId }
        , _pin{ pin }
    {
    }

    bool AuthorizingStep::processImpl()
    {
        const auto& channel = getChannelByEcuId(_cmId, _channels);
        channel.clearRx();
        UDSMessage requestSeedMsg(_cmId, { 0x27, 0x01 });
        unsigned long numMsgs;
        if (channel.writeMsgs(requestSeedMsg, numMsgs) != STATUS_NOERROR || numMsgs < 1) {
            return false;
        }
        const auto seedResponse = readMessageCheckAndGet(channel, { 0x67 }, { 0x01 });
        std::array<uint8_t, 3> seed = { seedResponse[0], seedResponse[1], seedResponse[2] };
        uint32_t key = generateKey(_pin, seed);
        channel.clearRx();
        UDSMessage sendKeyMsg(_cmId, { 0x27, 0x02, (key >> 16) & 0xFF, (key >> 8) & 0xFF, key & 0xFF });
        if (channel.writeMsgs(sendKeyMsg, numMsgs) != STATUS_NOERROR || numMsgs < 1) {
            return false;
        }
        return readMessageAndCheck(channel, { 0x67 }, { 0x02 }, 10);
    }

    uint32_t AuthorizingStep::generateKeyImpl(uint32_t hash, uint32_t input)
    {
        for (size_t i = 0; i < 32; ++i)
        {
            const bool is_bit_set = (hash ^ input) & 1;
            input >>= 1;
            hash >>= 1;
            if (is_bit_set)
                hash = (hash | 0x800000) ^ 0x109028;
        }
        return hash;
    }

    uint32_t AuthorizingStep::generateKey(const std::array<uint8_t, 5>& pin_array, const std::array<uint8_t, 3>& seed_array)
    {
        const uint32_t high_part = pin_array[4] << 24 | pin_array[3] << 16 | pin_array[2] << 8 | pin_array[1];
        const uint32_t low_part = pin_array[0] << 24 | seed_array[2] << 16 | seed_array[1] << 8 | seed_array[0];
        unsigned int hash = 0xC541A9;
        hash = generateKeyImpl(hash, low_part);
        hash = generateKeyImpl(hash, high_part);
        uint32_t result = ((hash & 0xF00000) >> 12) | hash & 0xF000 | (uint8_t)(16 * hash)
            | ((hash & 0xFF0) << 12) | ((hash & 0xF0000) >> 16);
        return result;
    }


    DataTransferStep::DataTransferStep(UDSStepType step, const std::vector<std::unique_ptr<j2534::J2534Channel>>& channels, uint32_t cmId,
        const VBF& data)
        : UDSProtocolStep{ step, getMaximumSize(data), true }
        , _channels{ channels }
        , _cmId{ cmId }
        , _data{ data }
    {
    }

    bool DataTransferStep::processImpl()
    {
        const auto& channel = getChannelByEcuId(_cmId, _channels);
        for (const auto& chunk : _data.chunks) {
            const auto startAddr = chunk.writeOffset;
            const auto dataSize = chunk.data.size();
            UDSMessage requestDownloadMsg(_cmId, { 0x34, 0x00, 0x44,
                (startAddr >> 24) & 0xFF, (startAddr >> 16) & 0xFF, (startAddr >> 8) & 0xFF, startAddr & 0xFF,
                (dataSize >> 24) & 0xFF, (dataSize >> 16) & 0xFF, (dataSize >> 8) & 0xFF, dataSize & 0xFF });
            unsigned long numMsgs;
            channel.clearRx();
            if (channel.writeMsgs(requestDownloadMsg, numMsgs) != STATUS_NOERROR || numMsgs < 1) {
                return false;
            }
            const auto downloadResponse = readMessageCheckAndGet(channel, { 0x74 }, { 0x20 }, 10);
            if (downloadResponse.empty()) {
                return false;
            }
            const size_t maxSizeToTransfer = encode(downloadResponse[1], downloadResponse[0]) - 2;
            uint8_t chunkIndex = 1;
            for (size_t i = 0; i < chunk.data.size(); i += maxSizeToTransfer, ++chunkIndex) {
                const auto chunkEnd = std::min(i + maxSizeToTransfer, chunk.data.size());
                std::vector<uint8_t> data{ 0x36, chunkIndex };
                data.insert(data.end(), chunk.data.cbegin() + i, chunk.data.cbegin() + chunkEnd);
                UDSMessage transferMsg(_cmId, std::move(data));
                channel.clearRx();
                if (channel.writeMsgs(transferMsg, numMsgs, 60000) != STATUS_NOERROR || numMsgs < 1) {
                    return false;
                }
                if (!readMessageAndCheck(channel, { 0x76 }, { chunkIndex }, 10)) {
                    return false;
                }
            }
            UDSMessage transferExitMsg(_cmId, { 0x37 });
            channel.clearRx();
            if (channel.writeMsgs(transferExitMsg, numMsgs) != STATUS_NOERROR || numMsgs < 1) {
                return false;
            }
            if (!readMessageAndCheck(channel, { 0x77 }, { static_cast<uint8_t>(chunk.crc >> 8), static_cast<uint8_t>(chunk.crc) }, 10)) {
                return false;
            }
        }

        return true;
    }

    size_t DataTransferStep::getMaximumSize(const VBF& data)
    {
        size_t result = 0;
        for (const auto chunk : data.chunks) {
            result += chunk.data.size();
        }
        return result;
    }

    FlashErasingStep::FlashErasingStep(const std::vector<std::unique_ptr<j2534::J2534Channel>>& channels, uint32_t cmId, const VBF& flash)
        : UDSProtocolStep{ UDSStepType::FlashErasing, 100 * flash.chunks.size(), true }
        , _channels{ channels }
        , _cmId{ cmId }
        , _flash{ flash }
    {
    }

    bool FlashErasingStep::processImpl()
    {
        const auto& channel = getChannelByEcuId(_cmId, _channels);
        for (const auto& chunk : _flash.chunks) {
            const auto eraseAddr = toVector(chunk.writeOffset);
            const auto eraseSize = toVector(static_cast<uint32_t>(chunk.data.size()));
            UDSMessage eraseRoutineMsg(_cmId, { 0x31, 0x01, 0xff, 0x00,
                eraseAddr[0], eraseAddr[1], eraseAddr[2], eraseAddr[3],
                eraseSize[0], eraseSize[1], eraseSize[2], eraseSize[3] });
            unsigned long numMsgs;
            if (channel.writeMsgs(eraseRoutineMsg, numMsgs) != STATUS_NOERROR || numMsgs < 1) {
                return false;
            }
            if (!readMessageAndCheck(channel, { 0x71, 0x01, 0xff, 0x00, 0x00, 0x00 }, {}, 10)) {
                return false;
            }
        }
        return true;
    }


    StartRoutineStep::StartRoutineStep(const std::vector<std::unique_ptr<j2534::J2534Channel>>& channels, uint32_t cmId, const common::VBFHeader& header)
        : UDSProtocolStep{ common::UDSStepType::BootloaderStarting, 100, true }
        , _channels{ channels }
        , _cmId{ cmId }
        , _header{ header }
    {
    }

    bool StartRoutineStep::processImpl()
    {
        const auto& channel = getChannelByEcuId(_cmId, _channels);
        const auto callAddr = common::toVector(_header.call);
        common::UDSMessage startRoutineMsg(_cmId, { 0x31, 0x01, 0x03, 0x01, callAddr[0], callAddr[1], callAddr[2], callAddr[3] });
        unsigned long numMsgs;
        if (channel.writeMsgs(startRoutineMsg, numMsgs) != STATUS_NOERROR || numMsgs < 1) {
            return false;
        }
        if (!common::readMessageAndCheck(channel, { 0x71, 0x01, 0x03, 0x01 }, {}, 10)) {
            return false;
        }
        return true;
    }

} // namespace common
