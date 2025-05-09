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

    uint32_t encodeBigEndian(uint8_t byte1, uint8_t byte2 = 0, uint8_t byte3 = 0,
        uint8_t byte4 = 0);
    uint32_t encodeLittleEndian(uint8_t byte1, uint8_t byte2, uint8_t byte3,
                             uint8_t byte4);
    uint32_t encodeBigEndian(const std::vector<uint8_t>& data);
    uint32_t encodeLittleEndian(const std::vector<uint8_t>& data);

    std::vector<uint8_t> toVector(uint16_t value);
    std::vector<uint8_t> toVector(uint32_t value);

    std::string toLower(std::string data);

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
        openUDSChannel(j2534::J2534& j2534, unsigned long Baudrate, uint32_t canId = 0);

    bool prepareUDSChannel(const j2534::J2534Channel& channel, uint32_t canId);
    bool prepareTP20Channel(const j2534::J2534Channel& channel, uint32_t canId);

    std::unique_ptr<j2534::J2534Channel>
    openTP20Channel(j2534::J2534& j2534, unsigned long Baudrate, uint32_t canId = 0);

    std::unique_ptr<j2534::J2534Channel> openLowSpeedChannel(j2534::J2534& j2534,
        unsigned long Flags);

    std::unique_ptr<j2534::J2534Channel> openBridgeChannel(j2534::J2534& j2534);

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

    CarPlatform parseCarPlatform(std::string input);

    ConfigurationInfo getConfigurationInfoByCarPlatform(CarPlatform carPlatform);

    std::tuple<BusConfiguration, ECUInfo> getEcuInfoByEcuId(CarPlatform carPlatform, uint32_t ecuId);

    j2534::J2534Channel& getChannelByEcuId(CarPlatform carPlatform, uint32_t ecuId,
                                           const std::vector<std::unique_ptr<j2534::J2534Channel>>& channels);

    size_t getChannelIndexByEcuId(CarPlatform carPlatform, uint32_t ecuId,
                                  const std::vector<std::unique_ptr<j2534::J2534Channel>>& channels);

    std::vector<ConfigurationInfo> loadConfiguration(std::istream& input);
    std::vector<ConfigurationInfo> loadConfiguration(const std::string& input);

    void checkTP20Error(uint8_t requestId, const uint8_t* data, size_t dataSize);
    void checkUDSError(uint8_t requestId, const uint8_t* data, size_t dataSize);
    void checkD2Error(uint8_t ecuId, const std::vector<uint8_t>& requestId, const uint8_t* data, size_t dataSize);

    CarPlatform parseCarPlatform(std::string input);

    std::array<uint8_t, 5> getPinArray(uint64_t pin);

    void initLogger(const std::string& logFilename);

    uint16_t crc16(const uint8_t* data_p, size_t length);

} // namespace common
