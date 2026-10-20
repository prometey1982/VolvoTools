#pragma once

#include "DeviceInfo.hpp"
#include "CarPlatform.hpp"
#include "ConfigurationInfo.hpp"

#include <memory>
#include <optional>
#include <string>
#include <sstream>
#include <utility>
#include <vector>
#include <type_traits>
#include <ios>
#include <iomanip>

class ICanChannel;

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

    // canIdBitSize = 11 -> обычная адресация (ответ = canId + 0x8);
    // canIdBitSize = 29 -> 0x18DA{ps}{sa}, ответ 0x18DA{sa}{ps} (паритет ID меняется).
    std::unique_ptr<j2534::J2534Channel>
        openUDSChannel(j2534::J2534& j2534, unsigned long Baudrate, uint32_t canId = 0,
                       uint32_t canIdBitSize = 11);

    bool prepareUDSChannel(j2534::J2534Channel& channel, uint32_t canId, bool isExtendedId = false);

    // CAN ID ответа ЭБУ на запрос canId (для 11-bit: +0x8, для 29-bit: 0x18DA{ps}{sa} -> 0x18DA{sa}{ps}).
    uint32_t makeUDSResponseCanId(uint32_t canId, bool isExtendedId);
    bool prepareTP20Channel(j2534::J2534Channel& channel, uint32_t canId);

    std::unique_ptr<j2534::J2534Channel>
    openTP20Channel(j2534::J2534& j2534, unsigned long Baudrate, uint32_t canId = 0);

    std::unique_ptr<j2534::J2534Channel> openLowSpeedChannel(j2534::J2534& j2534,
        unsigned long Flags);

    std::unique_ptr<j2534::J2534Channel> openBridgeChannel(j2534::J2534& j2534);

    std::vector<uint8_t> readMessageCheckAndGet(
        ICanChannel& channel,
        const std::vector<uint8_t> msgId,
        const std::vector<uint8_t>& toCheck,
        size_t retryCount = 10);

    bool readMessageAndCheck(
        ICanChannel& channel,
        const std::vector<uint8_t> msgId,
        const std::vector<uint8_t>& toCheck,
        size_t retryCount = 10);

    CarPlatform getPlatfromFromVIN(const std::string& vin);

    // Модельный год по 10-му символу VIN: '1'..'9' -> 2001..2009, 'A'..'Y' (без I, O, Q, U, Z)
    // -> 2010..2030. Возвращает 0, если символ не распознан или VIN короче 10 символов.
    int getModelYearFromVIN(const std::string& vin);

    CarPlatform parseCarPlatform(std::string input);

    ConfigurationInfo getConfigurationInfoByCarPlatform(CarPlatform carPlatform);

    std::tuple<BusConfiguration, ECUInfo> getEcuInfoByEcuId(CarPlatform carPlatform, uint32_t ecuId);

    // Address (PS) якорных ЭБУ платформы — CEM и ECM. Берётся из data.yaml по префиксу
    // поля Name, поэтому новые платформы подхватываются без правки кода.
    // nullopt — такого ЭБУ на платформе нет (у P80 нет CEM, у SPA узлы не описаны).
    // protocol != NONE — искать только на шинах этого протокола: у P1_UDS и P2_UDS
    // два CEM с разными Address — на D2-шине (CAN) и на UDS-шине (ISO15765).
    std::optional<uint32_t> getCemEcuId(CarPlatform carPlatform,
                                        ProtocolType protocol = ProtocolType::NONE);
    std::optional<uint32_t> getEcmEcuId(CarPlatform carPlatform,
                                        ProtocolType protocol = ProtocolType::NONE);

    ICanChannel& getChannelByEcuId(CarPlatform carPlatform, uint32_t ecuId,
                                   const std::vector<std::unique_ptr<ICanChannel>>& channels);

    size_t getChannelIndexByEcuId(CarPlatform carPlatform, uint32_t ecuId,
                                  const std::vector<std::unique_ptr<ICanChannel>>& channels);

    std::vector<ConfigurationInfo> loadConfiguration(std::istream& input);
    std::vector<ConfigurationInfo> loadConfiguration(const std::string& input);

    void checkTP20Error(uint8_t requestId, const uint8_t* data, size_t dataSize);
    void checkUDSError(uint8_t requestId, const uint8_t* data, size_t dataSize);

    CarPlatform parseCarPlatform(std::string input);

    std::array<uint8_t, 5> getPinArray(uint64_t pin);

    void initLogger(const std::string& logFilename, bool enableConsole = false, bool debugMode = false);

    template<typename T>
    std::string dumpArray(const T& vec)
    {
        std::stringstream ss;
        ss << std::hex << std::setfill('0');
        for(const auto& i: vec) {
            ss << std::setw(2) << int(i) << " ";
        }
        return ss.str();
    }

} // namespace common
