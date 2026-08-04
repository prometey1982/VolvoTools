#include "flasher/UDSReaderMemory.hpp"

#include <common/CarPlatform.hpp>
#include <common/CanIdProvider.hpp>
#include <common/ICanChannel.hpp>
#include <common/protocols/UDSRequest.hpp>

#include <array>

namespace flasher {

UDSReaderMemory::UDSReaderMemory(j2534::J2534& j2534, common::CarPlatform carPlatform, uint32_t ecuId,
                                 ReadRanges ranges)
    : ReaderBase{ j2534, carPlatform, ecuId, ranges }
    , _canIdProvider{ common::createCanIdProviderForEcu(carPlatform, ecuId) }
{
}

void UDSReaderMemory::startImpl(std::vector<std::unique_ptr<common::ICanChannel>>& channels)
{
    auto& channel = *channels[0];
    auto funcCanId = _canIdProvider->getFuncCanId();
    auto physCanId = _canIdProvider->getPhysCanId();

    setCurrentState(FlasherState::ReadFlash);
    for(size_t i = 0; i < _ranges.size(); ++i) {
        auto& buffer = _buffers[i];
        buffer.clear();
        const ReadRange& range = _ranges[i];
        buffer.reserve(range.size);

        constexpr size_t blockSize = 0x100;  // 256 bytes per request
        for (size_t offset = 0; offset < range.size; offset += blockSize) {
            size_t chunkSize = std::min(blockSize, range.size - offset);
            uint32_t currentAddr = range.startAddr + static_cast<uint32_t>(offset);

            std::vector<uint8_t> requestData{ 0x23, 0x44,
                static_cast<uint8_t>((currentAddr >> 24) & 0xFF),
                static_cast<uint8_t>((currentAddr >> 16) & 0xFF),
                static_cast<uint8_t>((currentAddr >> 8) & 0xFF),
                static_cast<uint8_t>(currentAddr & 0xFF),
                static_cast<uint8_t>(chunkSize) };

            common::UDSRequest readRequest(physCanId, requestData);
            try {
                auto response = readRequest.process(channel);
                if (response.size() > 5) {
                    buffer.insert(buffer.end(), response.begin() + 5, response.end());
                }
            }
            catch (...) {
                throw std::runtime_error("UDSReaderMemory: read failed at offset " +
                                        std::to_string(offset));
            }
            incCurrentProgress(chunkSize);
        }
    }

    setCurrentState(FlasherState::Done);
}

} // namespace flasher
