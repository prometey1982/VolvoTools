#include "flasher/D2ReaderTF80.hpp"

#include <common/ICanChannel.hpp>
#include <common/protocols/D2Request.hpp>
#include <common/protocols/D2Messages.hpp>
#include <common/Util.hpp>
#include <j2534/J2534.hpp>

#define LOG_MODULE_NAME "flasher"
#include <common/LogHelper.hpp>

namespace flasher {

D2ReaderTF80::D2ReaderTF80(j2534::J2534& j2534, common::CarPlatform carPlatform, uint32_t ecuId,
                           ReadRanges ranges)
    : ReaderBase{ j2534, carPlatform, ecuId, ranges }
{
}

void D2ReaderTF80::startImpl(const std::vector<std::unique_ptr<common::ICanChannel>>& channels)
{
    auto& channel{ common::getChannelByEcuId(_carPlatform, _ecuId, channels) };
    setCurrentState(FlasherState::ReadFlash);
    for(size_t i = 0; i < _ranges.size(); ++i) {
        auto& buffer = _buffers[i];
        buffer.clear();
        auto range = _ranges[i];
        buffer.reserve(range.size);

        // XXX: TF80 0x0 addr read workaround
        if(range.startAddr == 0) {
            buffer.push_back(0);
            incCurrentProgress(1);
            ++range.startAddr;
            --range.size;
        }

        constexpr size_t chunkSize{ 132 };
        constexpr size_t maxErrorCount{ 10 };
        size_t errorCount{ 0 };
        size_t proccessedBytes{ 0 };
        for (uint32_t j = 0; j < range.size; j += proccessedBytes) {
            const uint32_t currentAddr = range.startAddr + j;
            const size_t requestSize = std::min(chunkSize, range.size - j);
            proccessedBytes = 0;
            common::D2Request readRequest{
                common::D2Messages::createReadTCMTF80DataByAddr(
                    currentAddr, requestSize) };
            try {
                auto response = readRequest.process(channel, 200, 3);
                constexpr size_t additionalShift{ 4 };
                if (response.size() > additionalShift) {
                    proccessedBytes =  std::min(requestSize, response.size() - additionalShift);
                    buffer.insert(buffer.end(), response.begin() + additionalShift,
                                  response.begin() + additionalShift + proccessedBytes);
                }
                incCurrentProgress(proccessedBytes);
                errorCount = 0;
            }
            catch(const std::exception& ex) {
                LOG_MODULE(ERROR) << ex.what();
                if(errorCount++ >= maxErrorCount) {
                    throw;
                }
            }
        }
    }

    setCurrentState(FlasherState::Done);
}

} // namespace flasher
