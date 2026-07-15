#include "flasher/pin/D2PinCrackerSteps.hpp"

#include "common/ICanChannel.hpp"
#include "common/protocols/D2Message.hpp"
#include "common/protocols/D2Messages.hpp"
#include "common/protocols/D2Request.hpp"
#include "common/protocols/D2ProtocolCommonSteps.hpp"
#include "common/Util.hpp"

#include <thread>

namespace flasher {

bool D2PinCrackerSteps::preAuth(common::ICanChannel& channel)
{
    auto funcCanId = _canIdProvider->getFuncCanId();

    // D2 wake pattern: periodic message on 0xFFFFE
    unsigned long msgId;
    if (!channel.startPeriodicMsg({funcCanId, {0xC8, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}}, 5, msgId)) {
        return false;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(500));
    channel.stopPeriodicMsg(msgId);
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
    (void)channel;
    (void)pin;

    // D2 does not use standard UDS 0x27 security access.
    // PIN cracking for D2 requires a custom implementation
    // specific to the target ECU's bootloader protocol.
    //
    // Typical approach for D2 PIN cracking:
    // 1. PreAuth (completed above)
    // 2. Try to enter primary bootloader (startPBL) or authorized session
    // 3. Send ECU-specific authentication command with the PIN
    // 4. Check response for success/failure
    // 5. If wrong PIN, ECU returns error or ignores the command
    //
    // This is NOT YET IMPLEMENTED because D2 authorization
    // varies by ECU and requires per-ECU testing.

    return false;
}

std::vector<unsigned long> D2PinCrackerSteps::startKeepAlive(common::ICanChannel& channel)
{
    (void)channel;
    return {};
}

void D2PinCrackerSteps::stopKeepAlive(std::vector<unsigned long>& ids)
{
    (void)ids;
}

} // namespace flasher
