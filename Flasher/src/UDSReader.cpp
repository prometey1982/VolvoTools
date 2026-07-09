#include "flasher/UDSReader.hpp"

#include <common/CarPlatform.hpp>
#include <common/CommonData.hpp>
#include <common/ECUInfo.hpp>
#include <common/ICanChannel.hpp>
#include <common/Util.hpp>
#include <common/protocols/UDSProtocolCommonSteps.hpp>
#include <common/protocols/UDSRequest.hpp>
#include <j2534/J2534.hpp>

#include <array>

namespace flasher {

UDSReader::UDSReader(j2534::J2534& j2534, common::CarPlatform carPlatform, uint32_t ecuId,
                     ReadRanges ranges, uint64_t pin)
    : ReaderBase{ j2534, carPlatform, ecuId, ranges }
    , _pin{ pin }
{
    const auto ecuInfo{ std::get<1>(common::getEcuInfoByEcuId(carPlatform, ecuId)) };
    _canId = ecuInfo.canId;
}

void UDSReader::startImpl(std::vector<std::unique_ptr<ICanChannel>>& channels)
{
    auto& channel = *channels[0];

    // Wake up
    setCurrentState(FlasherState::WakeUp);
    common::UDSProtocolCommonSteps::wakeUp(channels);

    // Fall asleep
    setCurrentState(FlasherState::FallAsleep);
    common::UDSProtocolCommonSteps::fallAsleep(channels);

    // Authorize if PIN is set
    if (_pin != 0) {
        setCurrentState(FlasherState::Authorize);
        auto pinArray = common::getPinArray(_pin);
        if (!common::UDSProtocolCommonSteps::authorize(channel, _canId, pinArray)) {
            throw std::runtime_error("UDSReader: authorization failed");
        }
    }

    // Read via 0x23 — ReadMemoryByAddress
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

            common::UDSRequest readRequest(_canId, requestData);
            try {
                auto response = readRequest.process(channel);
                if (response.size() > 5) {
                    buffer.insert(buffer.end(), response.begin() + 5, response.end());
                }
            }
            catch (...) {
                throw std::runtime_error("UDSReader: read failed at offset " +
                                        std::to_string(offset));
            }
            incCurrentProgress(chunkSize);
        }
    }

    // Wake up after read
    setCurrentState(FlasherState::WakeUp);
    common::UDSProtocolCommonSteps::wakeUp(channels);

    setCurrentState(FlasherState::Done);
}

} // namespace flasher
