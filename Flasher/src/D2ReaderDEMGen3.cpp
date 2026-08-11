#include "flasher/D2ReaderDEMGen3.hpp"
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

std::vector<common::CanFrame> writeMessagesAndReadMessages(common::ICanChannel& channel,
                                                           const common::CanFrame& msg,
                                                           size_t numberOfMessages)
{
    if (!channel.send(msg)) {
        throw std::runtime_error("write msgs error");
    }
    std::vector<common::CanFrame> response;
    response.reserve(numberOfMessages);
    if (!channel.receive(response, numberOfMessages, 10000)) {
        throw std::runtime_error("Failed to receive message");
    }
    return response;
}

} // namespace anonymous

namespace flasher {

D2ReaderDEMGen3::D2ReaderDEMGen3(j2534::J2534& j2534, common::CarPlatform carPlatform, uint32_t ecuId,
                                 ReadRanges ranges, common::VBF bootloader)
    : ReaderBase{ j2534, carPlatform, ecuId, std::move(ranges) }
    , _bootloader{ std::move(bootloader) }
{
}

void D2ReaderDEMGen3::startImpl(const std::vector<std::unique_ptr<common::ICanChannel>>& channels)
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

void D2ReaderDEMGen3::readStep(common::ICanChannel &channel, uint8_t ecuId)
{
    channel.clearRx();
    channel.clearTx();
    for (size_t r = 0; r < _ranges.size(); ++r) {
            auto& buffer = _buffers[r];
            buffer.clear();
            const auto& range = _ranges[r];
            buffer.reserve(range.size);

            size_t chunkSize{ 2048 };
            constexpr size_t payloadSize{ 6 };
            size_t errorCount{ 0 };
            for (uint32_t i = 0; i < range.size; i += chunkSize) {
                chunkSize = std::min(chunkSize, range.size - i);
                const auto currentPos = range.startAddr + i;
                const auto numberOfMessages = chunkSize / payloadSize + (chunkSize % payloadSize > 0 ? 1 : 0);
                const auto msg = common::D2RawMessages::createReadOffsetMsgDEM(
                    static_cast<uint8_t>(common::D2ECUType::DEM), currentPos);
                try {
                    const auto answer{ writeMessagesAndReadMessages(channel, msg, numberOfMessages) };
                    size_t bytesProcessed{ 0 };
                    for(const auto& response: answer) {
                        for(size_t s = 2; s < response.data.size() && bytesProcessed < chunkSize; ++s) {
                            buffer.push_back(response.data[s]);
                            incCurrentProgress(1);
                            ++bytesProcessed;
                        }
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
