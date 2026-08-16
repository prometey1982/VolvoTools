#include "flasher/pin/D2PinCrackerSteps.hpp"

#include "common/ICanChannel.hpp"
#include "common/protocols/D2Message.hpp"
#include "common/protocols/D2Messages.hpp"
#include "common/protocols/D2Request.hpp"
#include "common/protocols/D2ProtocolCommonSteps.hpp"
#include "common/KeyGenerators.hpp"
#include "common/Util.hpp"

#define LOG_MODULE_NAME "flasher"
#include <common/LogHelper.hpp>

#include <thread>

namespace flasher {

void D2PinCrackerSteps::fallAsleep(common::ICanChannel& channel, uint32_t funcCanId)
{
    LOG(DEBUG) << "fallAsleep: id: " << std::hex << funcCanId;
    std::vector<unsigned long> msgIds;
    unsigned long msgId;
    if (channel.startPeriodicMsg({funcCanId, {0x2, 0x10, 0x82}}, 5, msgId)) {
        msgIds.push_back(msgId);
    }
    if (channel.startPeriodicMsg({common::D2Message::CanId, {0xFF, 0x86, 0, 0, 0, 0, 0, 0}, true}, 5, msgId)) {
        msgIds.push_back(msgId);
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    for(const auto&id: msgIds) {
        channel.stopPeriodicMsg(id);
    }
}

std::vector<unsigned long> D2PinCrackerSteps::keepAlive(common::ICanChannel& channel,
                                                        uint32_t funcCanId)
{
    LOG(DEBUG) << "keepAlive: id: " << std::hex << funcCanId;
    std::vector<unsigned long> result;
    unsigned long msgId;
    if (channel.startPeriodicMsg({funcCanId, {0x2, 0x3E, 0x80}}, 1900, msgId)) {
        result.push_back(msgId);
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(500));
    channel.send({_canIdProvider->getPhysCanId(), {0x2, 0x10, 0x02}, true});
    common::CanFrame response;
    if(!channel.receive(response, 200)) {
        LOG(ERROR) << "Failed to 0x10, 0x02 response";
    }
    LOG(DEBUG) << "received msg: " << common::dumpArray(response.data);
    return result;
}

std::array<uint8_t, 3> D2PinCrackerSteps::requestSeed(common::ICanChannel& channel, uint32_t funcCanId)
{
    LOG(DEBUG) << "requestSeed: id: " << std::hex << funcCanId;
    if(!channel.send({funcCanId, {0x2, 0x27, 0x01}, true})) {
        throw std::runtime_error("Failed to send seed request");
    }
    common::CanFrame response;
    if(!channel.receive(response, 200)) {
        throw std::runtime_error("Failed to read seed response");
    }
    if(response.data.size() < 7) {
        throw std::runtime_error("Seed response size too small. 7 bytes required at least");
    }
    LOG(DEBUG) << "requestSeed: response: " << std::hex << common::dumpArray(response.data);
    return {response.data[4], response.data[5], response.data[6]};
}

bool D2PinCrackerSteps::authorize(common::ICanChannel& channel, uint32_t funcCanId, uint32_t key)
{
    LOG(DEBUG) << "authorize: key: " << std::hex << key;
    if(!channel.send({funcCanId, {0x5, 0x27, 0x02, (key >> 16) & 0xFF, (key >> 8) & 0xFF, key & 0xFF}, true})) {
        throw std::runtime_error("Failed to send key response");
    }
    common::CanFrame response;
    if(!channel.receive(response, 200)) {
        throw std::runtime_error("Failed to read key response");
    }
    if(response.data.size() < 5) {
        throw std::runtime_error("Key response size too small. 5 bytes required at least");
    }
    return response.data[4] == 0x02;
}

bool D2PinCrackerSteps::preAuth(common::ICanChannel& channel)
{
    fallAsleep(channel, _canIdProvider->getFuncCanId());
    return true;
}

void D2PinCrackerSteps::postAuth(common::ICanChannel& channel)
{
    auto funcCanId = _canIdProvider->getFuncCanId();

    unsigned long msgId;
    if (channel.startPeriodicMsg({funcCanId, {0xC8, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}}, 5, msgId)) {
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
        channel.stopPeriodicMsg(msgId);
    }
}

bool D2PinCrackerSteps::tryPin(common::ICanChannel& channel, uint64_t pin)
{
    const auto seed{ requestSeed(channel, _canIdProvider->getPhysCanId()) };
    auto pinArray = common::getPinArray(pin);
    uint32_t key = common::generateKeyVolvoFord(pinArray, seed);
    return authorize(channel, _canIdProvider->getPhysCanId(), key);
}

std::vector<unsigned long> D2PinCrackerSteps::startKeepAlive(common::ICanChannel& channel)
{
    return keepAlive(channel, _canIdProvider->getFuncCanId());
}

void D2PinCrackerSteps::stopKeepAlive(common::ICanChannel& channel, const std::vector<unsigned long>& ids)
{
    for(const auto&id: ids) {
        channel.stopPeriodicMsg(id);
    }
}

} // namespace flasher
