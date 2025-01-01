#pragma once

#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "DeviceInfo.hpp"
#include "CarPlatform.hpp"
#include "ConfigurationInfo.hpp"

namespace j2534 {
    class J2534;
    class J2534Channel;
} // namespace j2534

namespace common {

    std::wstring toWstring(const std::string& str);
    std::string toString(const std::wstring& str);

    uint32_t encode(uint8_t byte1, uint8_t byte2 = 0, uint8_t byte3 = 0,
        uint8_t byte4 = 0);

    std::vector<uint8_t> toVector(uint32_t value);

#ifdef UNICODE
    std::wstring toPlatformString(const std::string& str);
    std::string fromPlatformString(const std::wstring& str);
#else
    std::string toPlatformString(const std::string& str);
    std::string fromPlatformString(const std::string& str);
#endif

    std::vector<j2534::DeviceInfo> getAvailableDevices();

    std::unique_ptr<j2534::J2534Channel>
        openChannel(j2534::J2534& j2534, unsigned long ProtocolID, unsigned long Flags,
            unsigned long Baudrate, bool AdditionalConfiguration = false);

    std::unique_ptr<j2534::J2534Channel>
        openUDSChannel(j2534::J2534& j2534, unsigned long Baudrate, uint32_t cmId);

    std::unique_ptr<j2534::J2534Channel> openLowSpeedChannel(j2534::J2534& j2534,
        unsigned long Flags);

    std::unique_ptr<j2534::J2534Channel> openBridgeChannel(j2534::J2534& j2534);

    std::vector<uint8_t> readMessageSequence(j2534::J2534Channel& channel,
        size_t queryLength);

    std::vector<uint8_t> readMessageCheckAndGet(
        const j2534::J2534Channel& channel,
        const std::vector<uint8_t> msgId,
        const std::vector<uint8_t>& toCheck,
        size_t retryCount = 10);

    bool readMessageAndCheck(
        const j2534::J2534Channel& channel,
        const std::vector<uint8_t> msgId,
        const std::vector<uint8_t>& toCheck,
        size_t retryCount = 10);

    CarPlatform getPlatfromFromVIN(const std::string& vin);

    j2534::J2534Channel& getChannelByEcuId(uint32_t ecuId, const std::vector<std::unique_ptr<j2534::J2534Channel>>& channels);

    std::vector<ConfigurationInfo> loadConfiguration(std::istream& input);

} // namespace common
