#include "flasher/D2ReaderTF80.hpp"

#include <common/ICanChannel.hpp>
#include <common/protocols/D2Request.hpp>
#include <common/protocols/D2Messages.hpp>
#include <common/Util.hpp>
#include <j2534/J2534.hpp>

namespace flasher {

D2ReaderTF80::D2ReaderTF80(j2534::J2534& j2534, common::CarPlatform carPlatform, uint32_t ecuId,
                           ReadRanges ranges)
    : ReaderBase{ j2534, carPlatform, ecuId, ranges }
{
}

void D2ReaderTF80::startImpl(std::vector<std::unique_ptr<common::ICanChannel>>& channels)
{
    auto& channel = *channels[0];
    const uint8_t ecuId = static_cast<uint8_t>(_ecuId);

    setCurrentState(FlasherState::ReadFlash);
    for(size_t i = 0; i < _ranges.size(); ++i) {
        auto& buffer = _buffers[i];
        buffer.clear();
        const auto& range = _ranges[i];
        buffer.reserve(range.size);

        for (uint32_t j = 0; j < range.size; ++j) {
            const uint32_t currentAddr = range.startAddr + j;
            common::D2Request readRequest{
                common::D2Messages::createReadTCMTF80DataByAddr(
                    currentAddr, 1) };
            auto response = readRequest.process(channel, 200, 3);
            if (response.size() > 4) {
                buffer.push_back(response[4]);
            }
            incCurrentProgress(1);
        }
    }

    setCurrentState(FlasherState::Done);
}

} // namespace flasher
