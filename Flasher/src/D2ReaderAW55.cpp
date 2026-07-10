#include "flasher/D2ReaderAW55.hpp"

#include <common/ICanChannel.hpp>
#include <common/protocols/D2Request.hpp>
#include <common/protocols/D2Messages.hpp>
#include <common/Util.hpp>
#include <j2534/J2534.hpp>

namespace flasher {

D2ReaderAW55::D2ReaderAW55(j2534::J2534& j2534, common::CarPlatform carPlatform, uint32_t ecuId,
                           ReadRanges ranges)
    : ReaderBase{ j2534, carPlatform, ecuId, ranges }
{
}

void D2ReaderAW55::startImpl(std::vector<std::unique_ptr<common::ICanChannel>>& channels)
{
    auto& channel = *channels[0];
    const uint8_t ecuId = static_cast<uint8_t>(_ecuId);

    setCurrentState(FlasherState::ReadFlash);
    for(size_t i = 0; i < _ranges.size(); ++i) {
        auto& buffer = _buffers[i];
        buffer.clear();
        const ReadRange& range = _ranges[i];
        buffer.reserve(range.size);

        for (uint32_t j = 0; j < range.size; ++j) {
            const uint32_t currentAddr = range.startAddr + j;
            common::D2Request readRequest{
                common::D2Messages::createReadDataByOffsetMsg(
                    ecuId, currentAddr, 1) };
            auto response = readRequest.process(channel);
            if (!response.empty()) {
                buffer.push_back(response[0]);
            }
            incCurrentProgress(1);
        }
    }

    setCurrentState(FlasherState::Done);
}

} // namespace flasher
