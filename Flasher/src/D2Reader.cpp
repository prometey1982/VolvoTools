#include "flasher/D2Reader.hpp"

#include <common/ICanChannel.hpp>
#include <common/Util.hpp>
#include <common/protocols/D2ProtocolCommonSteps.hpp>
#include <common/VBFParser.hpp>
#include <j2534/J2534.hpp>
#include <j2534/J2534Channel.hpp>

#include <numeric>

namespace {

    constexpr uint32_t D2_CAN_ID = 0xFFFFE;

    CanFrame writeMessagesAndReadMessage(ICanChannel& channel,
                                          const CanFrame& msg) {
        channel.clearRx();
        if (!channel.send(msg)) {
            throw std::runtime_error("write msgs error");
        }
        CanFrame response;
        if (!channel.receive(response, 3000)) {
            throw std::runtime_error("Failed to receive message");
        }
        return response;
    }

} // namespace anonymous

namespace flasher {

D2Reader::D2Reader(j2534::J2534& j2534, common::CarPlatform carPlatform, uint32_t ecuId,
                   ReadRanges ranges, std::shared_ptr<SBLProviderBase> sblProvider)
    : ReaderBase{ j2534, carPlatform, ecuId, ranges }
    , _sblProvider{ std::move(sblProvider) }
{
}

void D2Reader::startImpl(std::vector<std::unique_ptr<ICanChannel>>& channels)
{
    auto bootloader = _sblProvider->getSBL(_carPlatform, _ecuId, {});
    auto& channel = *channels[0];
    const uint8_t ecuId = static_cast<uint8_t>(_ecuId);

    // D2 protocol steps (same sequence as D2FlasherBase)
    setCurrentState(FlasherState::WakeUp);
    common::D2ProtocolCommonSteps::wakeUp(channels);

    setCurrentState(FlasherState::FallAsleep);
    common::D2ProtocolCommonSteps::fallAsleep(channels);

    setCurrentState(FlasherState::OpenChannels);
    common::D2ProtocolCommonSteps::startPBL(channel, ecuId);

    // Load bootloader if needed
    if (!bootloader.chunks.empty()) {
        setCurrentState(FlasherState::LoadBootloader);
        common::D2ProtocolCommonSteps::transferData(channel, ecuId,
            bootloader, [](size_t) {});

        setCurrentState(FlasherState::StartBootloader);
        common::D2ProtocolCommonSteps::startRoutine(channel, ecuId,
            bootloader.header.call);
    }

    // Read bytes
    setCurrentState(FlasherState::ReadFlash);
    for(size_t i = 0; i < _ranges.size(); ++i) {
        auto& buffer = _buffers[i];
        buffer.clear();
        const auto& range = _ranges[i];
        buffer.reserve(range.size);

        for (uint32_t j = 0; j < range.size; ++j) {
            const auto currentPos = range.startAddr + j;
            common::D2ProtocolCommonSteps::jumpTo(channel, ecuId, currentPos);
            auto calcMsg = common::toVector(currentPos + 1);
            std::vector<uint8_t> payload(8, 0);
            payload[0] = ecuId;
            payload[1] = 0xB4;
            payload[2] = 0x15;
            payload[3] = 0x22;
            payload[4] = calcMsg[0];
            payload[5] = calcMsg[1];
            payload[6] = calcMsg[2];
            payload[7] = calcMsg[3];
            const auto checksumAnswer = writeMessagesAndReadMessage(channel,
                {D2_CAN_ID, std::move(payload), true});
            if (checksumAnswer.data.size() >= 2 && checksumAnswer.data[1] == 0xB1) {
                buffer.push_back(checksumAnswer.data[2]);
                incCurrentProgress(1);
            }
        }
    }

    // Wake up after read
    setCurrentState(FlasherState::WakeUp);
    common::D2ProtocolCommonSteps::wakeUp(channels);

    setCurrentState(FlasherState::Done);
}

} // namespace flasher
