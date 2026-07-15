#include "flasher/pin/UDSPinCrackerSteps.hpp"

#include "common/ICanChannel.hpp"
#include "common/Util.hpp"
#include "common/protocols/UDSRequest.hpp"
#include "common/protocols/UDSError.hpp"

#include <array>
#include <thread>

namespace flasher {

namespace {

uint32_t generateKeyImpl(uint32_t hash, uint32_t input)
{
    for (size_t i = 0; i < 32; ++i) {
        const bool is_bit_set = (hash ^ input) & 1;
        input >>= 1;
        hash >>= 1;
        if (is_bit_set)
            hash = (hash | 0x800000) ^ 0x109028;
    }
    return hash;
}

uint32_t generateKey(const std::array<uint8_t, 5>& pin_array, const std::array<uint8_t, 3>& seed_array)
{
    const uint32_t high_part = pin_array[4] << 24 | pin_array[3] << 16 | pin_array[2] << 8 | pin_array[1];
    const uint32_t low_part = pin_array[0] << 24 | seed_array[2] << 16 | seed_array[1] << 8 | seed_array[0];
    unsigned int hash = 0xC541A9;
    hash = generateKeyImpl(hash, low_part);
    hash = generateKeyImpl(hash, high_part);
    uint32_t result = ((hash & 0xF00000) >> 12) | hash & 0xF000 | (uint8_t)(16 * hash)
        | ((hash & 0xFF0) << 12) | ((hash & 0xF0000) >> 16);
    return result;
}

} // anonymous namespace

bool UDSPinCrackerSteps::preAuth(common::ICanChannel& channel)
{
    auto funcCanId = _canIdProvider->getFuncCanId();

    if (_needProgSession) {
        unsigned long msgId;
        if (!channel.startPeriodicMsg({funcCanId, {0x10, 0x81}}, 5, msgId)) {
            return false;
        }
        std::this_thread::sleep_for(std::chrono::seconds(2));
        channel.stopPeriodicMsg(msgId);
        return true;
    }

    unsigned long msgId;
    if (!channel.startPeriodicMsg({funcCanId, {0x10, 0x02}}, 5, msgId)) {
        return false;
    }
    std::this_thread::sleep_for(std::chrono::seconds(2));
    channel.stopPeriodicMsg(msgId);
    return true;
}

void UDSPinCrackerSteps::postAuth(common::ICanChannel& channel)
{
    auto funcCanId = _canIdProvider->getFuncCanId();
    for (const auto& idToWakeUp : {0x11, 0x81}) {
        unsigned long msgId;
        if (channel.startPeriodicMsg({funcCanId, {0x11, static_cast<uint8_t>(idToWakeUp)}}, 20, msgId)) {
            std::this_thread::sleep_for(std::chrono::milliseconds(200));
            channel.stopPeriodicMsg(msgId);
        }
    }
}

bool UDSPinCrackerSteps::tryPin(common::ICanChannel& channel, uint64_t pin)
{
    auto physCanId = _canIdProvider->getPhysCanId();
    auto pinArray = common::getPinArray(pin);

    for (size_t i = 0; i < 5; ++i) {
        try {
            channel.clearRx();
            common::UDSRequest seedRequest(physCanId, {0x27, 0x01});
            const auto seedResponse = seedRequest.process(channel);

            if (seedResponse.size() < 5)
                return false;

            std::array<uint8_t, 3> seed = {seedResponse[2], seedResponse[3], seedResponse[4]};
            uint32_t key = generateKey(pinArray, seed);

            channel.clearRx();
            common::UDSRequest keyRequest(physCanId, {0x27, 0x02,
                static_cast<uint8_t>((key >> 16) & 0xFF),
                static_cast<uint8_t>((key >> 8) & 0xFF),
                static_cast<uint8_t>(key & 0xFF)});

            try {
                const auto keyResponse = keyRequest.process(channel);
                bool result = keyResponse.size() >= 2 && keyResponse[1] == 0x02;
                return result;
            } catch (common::UDSError& error) {
                if (error.getErrorCode() != common::UDSError::ErrorCode::RequiredTimeDelayHasNotExpired) {
                    return false;
                }
            }
        } catch (...) {
            std::this_thread::sleep_for(std::chrono::seconds(1));
        }
    }
    return false;
}

std::vector<unsigned long> UDSPinCrackerSteps::startKeepAlive(common::ICanChannel& channel)
{
    auto funcCanId = _canIdProvider->getFuncCanId();
    _keepAliveIds.clear();
    unsigned long msgId;
    if (channel.startPeriodicMsg({funcCanId, {0x3E, 0x80}}, 1900, msgId)) {
        _keepAliveIds.push_back(msgId);
    }
    return _keepAliveIds;
}

void UDSPinCrackerSteps::stopKeepAlive(std::vector<unsigned long>& ids)
{
    (void)ids;
    _keepAliveIds.clear();
}

} // namespace flasher
