#include "flasher/D2ReaderME7.hpp"
#include "D2FlasherImpl.hpp"

#include <common/ICanChannel.hpp>
#include <common/Util.hpp>
#include <common/protocols/D2ProtocolCommonSteps.hpp>
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

D2ReaderME7::D2ReaderME7(j2534::J2534& j2534, common::CarPlatform carPlatform, uint32_t ecuId,
                         ReadRanges ranges, common::VBF bootloader)
    : ReaderBase{ j2534, carPlatform, ecuId, std::move(ranges) }
    , _bootloader{ std::move(bootloader) }
{
}

void D2ReaderME7::startImpl(std::vector<std::unique_ptr<ICanChannel>>& channels)
{
    D2FlasherImpl impl(channels, _carPlatform, static_cast<uint8_t>(_ecuId), _bootloader,
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
                    auto addr = common::toVector(currentPos + 1);
                    std::vector<uint8_t> payload = {0x7A, 0xBC};
                    payload.insert(payload.end(), addr.cbegin(), addr.cend());
                    const auto answer = writeMessagesAndReadMessage(channel,
                        {D2_CAN_ID, std::move(payload), true});
                    for(size_t s = 3; s < answer.data.size(); ++s) {
                        buffer.push_back(answer.data[s]);
                        incCurrentProgress(1);
                    }
                }
            }
        });

    impl.setMaximumFlashProgressValue(getMaximumProgress());
    impl.run();
}

} // namespace flasher
