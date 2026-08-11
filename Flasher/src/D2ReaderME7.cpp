#include "flasher/D2ReaderME7.hpp"
#include "D2FlasherImpl.hpp"

#include <common/ICanChannel.hpp>
#include <common/Util.hpp>
#include <common/protocols/D2ECUType.hpp>
#include <common/protocols/D2Message.hpp>
#include <common/protocols/D2Messages.hpp>
#include <common/protocols/D2ProtocolCommonSteps.hpp>
#include <j2534/J2534.hpp>
#include <j2534/J2534Channel.hpp>

#define LOG_MODULE_NAME "flasher"
#include <common/LogHelper.hpp>

#include <numeric>

namespace {

    common::CanFrame writeMessagesAndReadMessage(common::ICanChannel& channel,
                                          const common::CanFrame& msg) {
        channel.clearRx();
        if (!channel.send(msg)) {
            throw std::runtime_error("write msgs error");
        }
        common::CanFrame response;
        if (!channel.receive(response, 100)) {
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

void D2ReaderME7::startImpl(const std::vector<std::unique_ptr<common::ICanChannel>>& channels)
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
            readStep(channel, ecuId);
        });

    impl.setMaximumFlashProgressValue(getMaximumProgress());
    impl.run();
}

void D2ReaderME7::readStep(common::ICanChannel &channel, uint8_t ecuId)
{
    channel.clearRx();
    channel.clearTx();
    for (size_t r = 0; r < _ranges.size(); ++r) {
            auto& buffer = _buffers[r];
            buffer.clear();
            const auto& range = _ranges[r];
            buffer.reserve(range.size);

            size_t chunkSize{ 0 };
            size_t errorCount{ 0 };
            for (uint32_t i = 0; i < range.size; i += chunkSize) {
                chunkSize = 0;
                const auto currentPos = range.startAddr + i;
                const auto msg = common::D2RawMessages::createReadOffsetMsg2(
                    static_cast<uint8_t>(common::D2ECUType::ECM_ME), currentPos);
                try {
                    const auto answer = writeMessagesAndReadMessage(channel, msg);
                    for(size_t s = 2; s < answer.data.size(); ++s) {
                        buffer.push_back(answer.data[s]);
                        incCurrentProgress(1);
                        ++chunkSize;
                    }
                    errorCount = 0;
                }
                catch(const std::exception& ex) {
                    LOG_MODULE(ERROR) << ex.what();
                    if(errorCount++ >= 10) {
                        throw;
                    }
                }
            }
    }
}

} // namespace flasher
