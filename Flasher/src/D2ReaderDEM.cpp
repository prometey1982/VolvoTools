#include "flasher/D2ReaderDEM.hpp"
#include "D2FlasherImpl.hpp"

#include <common/ICanChannel.hpp>
#include <common/Util.hpp>
#include <common/protocols/D2ECUType.hpp>
#include <common/protocols/D2Message.hpp>
#include <common/protocols/D2Messages.hpp>
#include <common/protocols/D2ProtocolCommonSteps.hpp>
#include <j2534/J2534.hpp>
#include <j2534/J2534Channel.hpp>

#include <numeric>

namespace {

    common::CanFrame writeMessagesAndReadMessage(common::ICanChannel& channel,
                                          const common::CanFrame& msg) {
        channel.clearRx();
        if (!channel.send(msg)) {
            throw std::runtime_error("write msgs error");
        }
        common::CanFrame response;
        if (!channel.receive(response, 3000)) {
            throw std::runtime_error("Failed to receive message");
        }
        return response;
    }

} // namespace anonymous

namespace flasher {

D2ReaderDEM::D2ReaderDEM(j2534::J2534& j2534, common::CarPlatform carPlatform, uint32_t ecuId,
                         ReadRanges ranges, common::VBF bootloader)
    : ReaderBase{ j2534, carPlatform, ecuId, std::move(ranges) }
    , _bootloader{ std::move(bootloader) }
{
}

void D2ReaderDEM::startImpl(std::vector<std::unique_ptr<common::ICanChannel>>& channels)
{
    D2FlasherImpl impl(channels, _carPlatform, static_cast<uint8_t>(_ecuId), _bootloader,
        [this](FlasherState state) {
            setCurrentState(state);
        },
        [this](size_t progress) {
            incCurrentProgress(progress);
        },
        [](common::ICanChannel&, uint8_t) {},  // erase — no-op
        [this](common::ICanChannel& channel, uint8_t ecuId) {
            // write callback = byte-by-byte read for all ranges
            for (size_t r = 0; r < _ranges.size(); ++r) {
                auto& buffer = _buffers[r];
                buffer.clear();
                const auto& range = _ranges[r];
                buffer.reserve(range.size);

                for (uint32_t i = 0; i < range.size; ++i) {
                    const auto currentPos = range.startAddr + i;
                    const auto msg = common::D2RawMessages::createReadOffsetMsgDEM(
                        static_cast<uint8_t>(common::D2ECUType::DEM), currentPos);
                    const auto answer = writeMessagesAndReadMessage(channel, msg);
                    for(size_t s = 2; s < answer.data.size(); ++s) {
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
