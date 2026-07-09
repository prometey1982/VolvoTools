#include "flasher/D2ReaderChecksum.hpp"
#include "D2FlasherImpl.hpp"

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

D2ReaderChecksum::D2ReaderChecksum(j2534::J2534& j2534, common::CarPlatform carPlatform, uint32_t ecuId,
                   ReadRanges ranges)
    : ReaderBase{ j2534, carPlatform, ecuId, std::move(ranges) }
{
}

void D2ReaderChecksum::startImpl(std::vector<std::unique_ptr<ICanChannel>>& channels)
{
    D2FlasherImpl impl(channels, _carPlatform, static_cast<uint8_t>(_ecuId), common::VBF(),
        [this](FlasherState state) {
            setCurrentState(state);
        },
        [this](size_t progress) {
            incCurrentProgress(progress);
        },
        [](ICanChannel&, uint8_t) {},  // erase — no-op
        [this](ICanChannel& channel, uint8_t ecuId) {
            // write callback = byte-by-byte read for all ranges
            for (size_t r = 0; r < _ranges.size(); ++r) {
                auto& buffer = _buffers[r];
                buffer.clear();
                const auto& range = _ranges[r];
                buffer.reserve(range.size);

                for (uint32_t i = 0; i < range.size; ++i) {
                    const auto currentPos = range.startAddr + i;
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
                    }
                }
            }
        });

    size_t totalSize = 0;
    for (const auto& range : _ranges) {
        totalSize += range.size;
    }
    impl.setMaximumFlashProgressValue(totalSize);
    setMaximumProgress(impl.getMaximumProgress());
    impl.run();
}

} // namespace flasher
