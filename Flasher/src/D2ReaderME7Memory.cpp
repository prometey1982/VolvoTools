#include "flasher/D2ReaderME7Memory.hpp"

#include <common/ICanChannel.hpp>
#include <common/protocols/D2Request.hpp>
#include <common/protocols/D2Messages.hpp>
#include <common/Util.hpp>
#include <j2534/J2534.hpp>

namespace flasher {

D2ReaderME7Memory::D2ReaderME7Memory(j2534::J2534& j2534, common::CarPlatform carPlatform, uint32_t ecuId,
                           ReadRanges ranges)
    : ReaderBase{ j2534, carPlatform, ecuId, ranges }
{
}

void D2ReaderME7Memory::startImpl(std::vector<std::unique_ptr<common::ICanChannel>>& channels)
{
    auto& channel{ common::getChannelByEcuId(_carPlatform, _ecuId, channels) };
    const uint8_t ecuId{ static_cast<uint8_t>(_ecuId) };
    constexpr uint32_t memoryChunk{ 8 };

    setCurrentState(FlasherState::ReadFlash);
    for(size_t i = 0; i < _ranges.size(); ++i) {
        auto& buffer = _buffers[i];
        buffer.clear();
        const ReadRange& range = _ranges[i];
        buffer.reserve(range.size);

        for (uint32_t j = 0; j < range.size; j += memoryChunk) {
            const uint32_t currentAddr{ range.startAddr + j };
            const auto currentChunkSize{ std::min(range.size - j, memoryChunk) };
            common::D2Request readRequest{
                common::D2Messages::createReadDataByAddrMsg(
                    ecuId, currentAddr, currentChunkSize) };
            auto response = readRequest.process(channel);
            if (!response.empty()) {
                buffer.insert(buffer.end(), response.data(), response.data() + currentChunkSize);
            }
            incCurrentProgress(currentChunkSize);
        }
    }

    setCurrentState(FlasherState::Done);
}

} // namespace flasher
