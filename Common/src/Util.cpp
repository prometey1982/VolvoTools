#include "common/Util.hpp"

#include "common/CommonData.hpp"
#include "common/BusConfiguration.hpp"
#include "common/protocols/D2Error.hpp"
#include "common/protocols/TP20Error.hpp"
#include "common/protocols/UDSError.hpp"
#include "common/ECUInfo.hpp"

#include <Registry/include/Registry.hpp>
#include <j2534/J2534Channel.hpp>
#include <j2534/J2534_v0404.h>

#include <yaml-cpp/yaml.h>

#include <easylogging++.h>

#include <algorithm>
#include <codecvt>
#include <locale>
#include <unordered_map>
#include <fstream>

namespace common {

    std::wstring toWstring(const std::string& str) {
        using convert_type = std::codecvt_utf8<wchar_t>;
        std::wstring_convert<convert_type, wchar_t> converter;
        return converter.from_bytes(str);
    }

    std::string toString(const std::wstring& str) {
        using convert_type = std::codecvt_utf8<wchar_t>;
        std::wstring_convert<convert_type, wchar_t> converter;
        return converter.to_bytes(str);
    }

    uint32_t encodeBigEndian(uint8_t byte1, uint8_t byte2, uint8_t byte3, uint8_t byte4) {
        return byte1 + (byte2 << 8) + (byte3 << 16) + (byte4 << 24);
    }

    uint32_t encodeLittleEndian(uint8_t byte1, uint8_t byte2, uint8_t byte3, uint8_t byte4) {
        return byte4 + (byte3 << 8) + (byte2 << 16) + (byte1 << 24);
    }

    uint32_t encodeBigEndian(const std::vector<uint8_t>& data) {
        uint32_t result{};
        auto it = data.cbegin();
        for(size_t i = 0; i < 4; ++i) {
            result += (*it) << (i * 8);
            ++it;
            if(it == data.cend()) {
                break;
            }
        }
        return result;
    }

    uint32_t encodeLittleEndian(const std::vector<uint8_t>& data) {
        uint32_t result{};
        auto it = data.crbegin();
        for(size_t i = 0; i < 4; ++i) {
            result += ((*it) << (i * 8));
            ++it;
            if(it == data.crend()) {
                break;
            }
        }
        return result;
    }

    std::vector<uint8_t> toVector(uint16_t value) {
        const uint8_t byte1 = (value & 0xFF00) >> 8;
        const uint8_t byte2 = (value & 0xFF);
        return { byte1, byte2 };
    }

    std::vector<uint8_t> toVector(uint32_t value) {
        const uint8_t byte1 = (value & 0xFF000000) >> 24;
        const uint8_t byte2 = (value & 0xFF0000) >> 16;
        const uint8_t byte3 = (value & 0xFF00) >> 8;
        const uint8_t byte4 = (value & 0xFF);
        return { byte1, byte2, byte3, byte4 };
    }

    std::string toLower(std::string data) {
        std::transform(data.begin(), data.end(), data.begin(),
            [](unsigned char c) { return std::tolower(c); });
        return data;
    }

#ifdef UNICODE
    std::wstring toPlatformString(const std::string& str) { return toWstring(str); }

    std::string fromPlatformString(const std::wstring& str) {
        return toString(str);
    }
#else
    std::string toPlatformString(const std::string& str) { return str; }

    std::string fromPlatformString(const std::string& str) { return str; }
#endif

    using namespace m4x1m1l14n::Registry;

    static bool processRegistry(const std::string& keyName,
        std::string& libraryPath,
        std::vector<std::string>& deviceNames) {
        const auto platformKeyName = toPlatformString(keyName);
        const auto key = LocalMachine->Open(platformKeyName);
        try {
            auto localLibraryPath =
                fromPlatformString(key->GetString(TEXT("FunctionLibrary")));
            if (!localLibraryPath.empty()) {
                libraryPath = localLibraryPath;
                key->EnumerateSubKeys(
                    [&deviceNames, &platformKeyName](const auto& deviceKeyName) {
                        const auto key = LocalMachine->Open(
                            platformKeyName + toPlatformString("\\") + deviceKeyName);
                        const auto deviceName =
                            fromPlatformString(key->GetString(TEXT("Name")));
                        deviceNames.push_back(deviceName);
                        return true;
                    });
                if (deviceNames.empty()) {
                    const auto deviceName =
                        fromPlatformString(key->GetString(TEXT("Name")));
                    deviceNames.push_back(deviceName);
                }
            }
        }
        catch (...) {
            return true;
        }
        return !libraryPath.empty();
    }

    std::vector<j2534::DeviceInfo> getAvailableDevices() {
        std::vector<j2534::DeviceInfo> result;
#if 0
        result.push_back({ "C:\\Program Files (x86)\\DiCE\\Tools\\TSDiCE32.dll", "DiCE-206751" });
#else
        const std::string rootKeyName{ "Software\\PassThruSupport.04.04" };
        try {
            const auto key = LocalMachine->Open(toPlatformString(rootKeyName));
            key->EnumerateSubKeys([&rootKeyName, &result](const auto& subKeyName) {
                std::string libraryPath;
                std::vector<std::string> deviceNames;
                auto res =
                    processRegistry(rootKeyName + "\\" + fromPlatformString(subKeyName),
                        libraryPath, deviceNames);
                if (res) {
                    for (const auto& deviceName : deviceNames) {
                        result.push_back({ libraryPath, deviceName });
                    }
                }
                return res;
                });
        }
        catch (...)
        {
        }

#endif
        return result;
    }

    static PASSTHRU_MSG makePassThruMsg(unsigned long ProtocolID,
        unsigned long Flags,
        const std::vector<unsigned char>& data) {
        PASSTHRU_MSG result;
        result.ProtocolID = ProtocolID;
        result.RxStatus = 0;
        result.TxFlags = Flags;
        result.Timestamp = 0;
        result.ExtraDataIndex = 0;
        result.DataSize = data.size();
        std::copy(data.begin(), data.end(), result.Data);
        return result;
    }

    static std::vector<PASSTHRU_MSG>
        makePassThruMsgs(unsigned long ProtocolID, unsigned long Flags,
            const std::vector<std::vector<unsigned char>>& data) {
        std::vector<PASSTHRU_MSG> result;
        for (const auto msgData : data) {
            PASSTHRU_MSG msg;
            msg.ProtocolID = ProtocolID;
            msg.RxStatus = 0;
            msg.TxFlags = Flags;
            msg.Timestamp = 0;
            msg.ExtraDataIndex = 0;
            msg.DataSize = msgData.size();
            std::copy(msgData.begin(), msgData.end(), msg.Data);
            result.emplace_back(std::move(msg));
        }
        return result;
    }

    static void startXonXoffMessageFiltering(j2534::J2534Channel& channel,
        unsigned long Flags) {
        auto msgs{
            makePassThruMsgs(CAN_XON_XOFF, Flags,
                             {
                                 {0x00, 0x00, 0x00, 0x01, 0x00, 0xff, 0xff, 0x00},
                                 {0x00, 0x00, 0x00, 0x01, 0x00, 0xA9, 0x00, 0x00},
                                 {0x00, 0x00, 0x00, 0x01, 0x00, 0xff, 0xff, 0x00},
                                 {0x00, 0x00, 0x00, 0x01, 0x00, 0xA9, 0x01, 0x00},
                                 {0x00, 0x00, 0x00, 0x01, 0x00, 0xff, 0xff, 0x00},
                                 {0x00, 0x00, 0x00, 0x01, 0x00, 0xA9, 0x02, 0x00},
                             }) };

        channel.passThruIoctl(CAN_XON_XOFF_FILTER, msgs.data());
        unsigned long msgId = 0;
        channel.passThruIoctl(CAN_XON_XOFF_FILTER_ACTIVE, &msgId);
    }

    static void setupChannelParameters(j2534::J2534Channel& channel)
    {
        std::vector<SCONFIG> config(3);
        config[0].Parameter = DATA_RATE;
        config[0].Value = channel.getBaudrate();
        config[1].Parameter = LOOPBACK;
        config[1].Value = 0;
        config[2].Parameter = BIT_SAMPLE_POINT;
        config[2].Value = (channel.getBaudrate() == 500000 ? 80 : 68);
        channel.setConfig(config);
    }

    static void setupChannelPins(j2534::J2534Channel& channel)
    {
        if (channel.getProtocolId() == ISO14230_PS || channel.getProtocolId() == ISO15765_PS) {
            std::vector<SCONFIG> config(1);
            config[0].Parameter = J1962_PINS;
            config[0].Value = 0x030B;
            channel.setConfig(config);
        }
    }

    std::unique_ptr<j2534::J2534Channel>
        openChannel(j2534::J2534& j2534, unsigned long ProtocolID, unsigned long Flags,
            unsigned long Baudrate, bool AdditionalConfiguration) {
        auto channel{ std::make_unique<j2534::J2534Channel>(j2534, ProtocolID, Flags,
                                                           Baudrate, Flags) };

        setupChannelParameters(*channel);

        unsigned long msgId;
        PASSTHRU_MSG msgFilter =
            makePassThruMsg(ProtocolID, Flags, { 0x00, 0x00, 0x00, 0x01 });
        channel->startMsgFilter(PASS_FILTER, &msgFilter, &msgFilter, nullptr, msgId);

        if (AdditionalConfiguration && ProtocolID == CAN_XON_XOFF) {
            startXonXoffMessageFiltering(*channel, Flags);
            std::vector<SCONFIG> config(1);
            config[0].Parameter = CAN_XON_XOFF;
            config[0].Value = 0;
            channel->setConfig(config);
        }
        return std::move(channel);
    }

    std::unique_ptr<j2534::J2534Channel>
        openUDSChannel(j2534::J2534& j2534, unsigned long baudrate, uint32_t canId) {

        std::unique_ptr<j2534::J2534Channel> channel;
        const std::vector<unsigned long> SupportedProtocols = { ISO15765_PS, ISO15765 };
        for (const auto& protocolId : SupportedProtocols) {
            if(protocolId == ISO15765_PS && baudrate != 125000) {
                continue;
            }
            unsigned long flags = 0;
            unsigned long txFlags = ISO15765_FRAME_PAD;
            if (protocolId == ISO15765 && baudrate == 125000)
                flags |= PHYSICAL_CHANNEL;
            try {
                channel = std::make_unique<j2534::J2534Channel>(
                    j2534, protocolId, flags, baudrate, txFlags);
            }
            catch (...) {
                continue;
            }

            setupChannelParameters(*channel);
            setupChannelPins(*channel);

            if (canId) {
                prepareUDSChannel(*channel, canId);
            }

            return std::move(channel);
        }
        return {};
    }

    bool prepareUDSChannel(const j2534::J2534Channel& channel, uint32_t canId) {
        const uint32_t responseCanId = canId + 0x8;
        unsigned long msgId;
        PASSTHRU_MSG maskMsg =
            makePassThruMsg(channel.getProtocolId(), channel.getTxFlags(), { 0xFF, 0xFF, 0xFF, 0xFF });
        PASSTHRU_MSG patternMsg =
            makePassThruMsg(channel.getProtocolId(), channel.getTxFlags(), toVector(responseCanId));
        PASSTHRU_MSG flowMsg =
            makePassThruMsg(channel.getProtocolId(), channel.getTxFlags(), toVector(canId));
        return channel.startMsgFilter(FLOW_CONTROL_FILTER, &maskMsg, &patternMsg, &flowMsg, msgId) == STATUS_NOERROR;
    }

    bool prepareTP20Channel(const j2534::J2534Channel& channel, uint32_t canId) {
        unsigned long msgId;
        PASSTHRU_MSG maskMsg =
            makePassThruMsg(channel.getProtocolId(), channel.getTxFlags(), { 0xFF, 0xFF, 0xFF, 0xFF });
        PASSTHRU_MSG patternMsg =
            makePassThruMsg(channel.getProtocolId(), channel.getTxFlags(), toVector(canId));
        return channel.startMsgFilter(PASS_FILTER, &maskMsg, &patternMsg, nullptr, msgId) == STATUS_NOERROR;
    }

    std::unique_ptr<j2534::J2534Channel>
    openTP20Channel(j2534::J2534& j2534, unsigned long baudrate, uint32_t canId) {

        std::unique_ptr<j2534::J2534Channel> channel;
        const std::vector<unsigned long> SupportedProtocols = { CAN_PS, CAN };
        for (const auto& protocolId : SupportedProtocols) {
            if(protocolId == CAN_PS && baudrate != 125000) {
                continue;
            }
            unsigned long flags = 0;
            if (protocolId == CAN && baudrate == 125000)
                flags |= PHYSICAL_CHANNEL;
            try {
                channel = std::make_unique<j2534::J2534Channel>(
                    j2534, protocolId, flags, baudrate, 0);
            }
            catch (...) {
                continue;
            }

//            setupChannelParameters(*channel);
            setupChannelPins(*channel);

            if (canId) {
                prepareTP20Channel(*channel, canId);
            }

            return std::move(channel);
        }
        return {};
    }

    std::unique_ptr<j2534::J2534Channel> openLowSpeedChannel(j2534::J2534& j2534,
        unsigned long Flags) {

        const auto Baudrate = 125000;
        const std::vector<unsigned long> SupportedProtocols = { CAN_XON_XOFF, CAN_PS };
        std::unique_ptr<j2534::J2534Channel> channel;

        for (const auto& ProtocolID : SupportedProtocols) {
            auto LocalFlags = Flags;
            if (ProtocolID == CAN_XON_XOFF)
                LocalFlags |= PHYSICAL_CHANNEL;
            try {
                channel = std::make_unique<j2534::J2534Channel>(
                    j2534, ProtocolID, LocalFlags, Baudrate, Flags);
            }
            catch (...) {
                continue;
            }

            if (ProtocolID == CAN_PS) {
                std::vector<SCONFIG> config(1);
                config[0].Parameter = J1962_PINS;
                config[0].Value = 0x030B;
                channel->setConfig(config);
            }
            setupChannelParameters(*channel);

            PASSTHRU_MSG msgFilter =
                makePassThruMsg(ProtocolID, Flags, { 0x00, 0x00, 0x00, 0x01 });
            unsigned long msgId;
            channel->startMsgFilter(PASS_FILTER, &msgFilter, &msgFilter, nullptr,
                msgId);
            PASSTHRU_MSG msgFilter2 =
                makePassThruMsg(ProtocolID, Flags, { 0x00, 0x00, 0x07, 0x00 });
            channel->startMsgFilter(PASS_FILTER, &msgFilter2, &msgFilter2, nullptr, msgId);
            return std::move(channel);
        }
        return {};
    }

    std::unique_ptr<j2534::J2534Channel> openBridgeChannel(j2534::J2534& j2534) {
        const unsigned long ProtocolId = ISO9141;
        const unsigned long Flags = ISO9141_K_LINE_ONLY;
        auto channel{ std::make_unique<j2534::J2534Channel>(j2534, ProtocolId, Flags,
                                                           10400, Flags) };
        std::vector<SCONFIG> config(4);
        config[0].Parameter = PARITY;
        config[0].Value = 0;
        config[1].Parameter = W0;
        config[1].Value = 60;
        config[2].Parameter = W1;
        config[2].Value = 600;
        config[3].Parameter = P4_MIN;
        config[3].Value = 0;
        channel->setConfig(config);

        PASSTHRU_MSG msg =
            makePassThruMsg(ProtocolId, Flags, { 0x84, 0x40, 0x13, 0xb2, 0xf0, 0x03 });
        unsigned long msgId;
        channel->startPeriodicMsg(msg, msgId, 2000);

        return std::move(channel);
    }

    static bool readCheckAndGetImpl(
        const j2534::J2534Channel& channel,
        const std::vector<uint8_t> msgId,
        const std::vector<uint8_t>& toCheck,
        std::vector<uint8_t>& result,
        size_t retryCount) {
        for (size_t i = 0; i < retryCount; ++i) {
            std::vector<PASSTHRU_MSG> read_msgs;
            read_msgs.resize(1);
            if (channel.readMsgs(read_msgs, 10000) != STATUS_NOERROR || read_msgs.empty())
            {
                continue;
            }
            const auto& msg = read_msgs[0];
            if (msg.DataSize < msgId.size() + 4) {
                continue;
            }
            uint32_t checkOffset = 4;
            const auto areMessagesEqual = std::equal(msgId.cbegin(), msgId.cend(), msg.Data + checkOffset);
            if (!areMessagesEqual) {
                continue;
            }
            checkOffset += msgId.size();
            const auto areResultEqual = std::equal(toCheck.cbegin(), toCheck.cend(), msg.Data + checkOffset);
            if (!areResultEqual) {
                return false;
            }
            else {
                checkOffset += toCheck.size();
                result.insert(result.end(), msg.Data + checkOffset, msg.Data + msg.DataSize);
                return true;
            }
        }
        return false;
    }

    std::vector<uint8_t> readMessageCheckAndGet(
        const j2534::J2534Channel& channel,
        const std::vector<uint8_t> msgId,
        const std::vector<uint8_t>& toCheck,
        size_t retryCount) {
        std::vector<uint8_t> result;
        readCheckAndGetImpl(channel, msgId, toCheck, result, retryCount);
        return result;
    }

    bool readMessageAndCheck(
        const j2534::J2534Channel& channel,
        const std::vector<uint8_t> msgId,
        const std::vector<uint8_t>& toCheck,
        size_t retryCount)
    {
        std::vector<uint8_t> result;
        return readCheckAndGetImpl(channel, msgId, toCheck, result, retryCount);
    }

    CarPlatform getPlatfromFromVIN(const std::string& vin)
    {
        CarPlatform result = CarPlatform::Undefined;
        const std::string volvoPrefix = "YV1";
        if (vin.find(volvoPrefix) == 0)
        {
            switch (vin[3])
            {
            case 'A':
                result = CarPlatform::P3;
                break;
            case 'L':
                result = CarPlatform::P80;
                break;
            case 'T':
            case 'R':
            case 'S':
            case 'C':
                result = CarPlatform::P2;
                break;
            case 'M':
                result = CarPlatform::P1;
                break;
            }
            return result;
        }
        const std::string fordPrefix = "WF";
        if (vin.find(fordPrefix) == 0)
        {
            switch(vin[8])
            {
            case 'B':
                result = CarPlatform::Ford_UDS;
                break;
            }
            return result;
        }
        const std::string havalPrefix = "XZG";
        if (vin.find(havalPrefix) == 0)
        {
            result = CarPlatform::Haval_UDS;
            return result;
        }
        return result;
    }

    CarPlatform parseCarPlatform(std::string input)
    {
        input = toLower(input);
        if ("p1" == input)
            return common::CarPlatform::P1;
        else if ("p1_uds" == input)
            return common::CarPlatform::P1_UDS;
        else if ("p2" == input)
            return common::CarPlatform::P2;
        else if ("p2_250" == input)
            return common::CarPlatform::P2_250;
        else if ("p2_uds" == input)
            return common::CarPlatform::P2_UDS;
        else if ("p3" == input)
            return common::CarPlatform::P3;
        else if ("spa" == input)
            return common::CarPlatform::SPA;
        else if ("ford_kwp" == input)
            return common::CarPlatform::Ford_KWP;
        else if ("ford_uds" == input)
            return common::CarPlatform::Ford_UDS;
        else if ("haval_uds" == input)
            return common::CarPlatform::Haval_UDS;
        return common::CarPlatform::Undefined;
    }

    static std::string getCarPlatformName(CarPlatform carPlatform)
    {
        switch (carPlatform) {
        case CarPlatform::P80:
            return "P80";
        case CarPlatform::P1:
            return "P1x (D2) - Elsys 1";
        case CarPlatform::P1_UDS:
            return "P1010 (D2/GGD) - Elsys 2";
        case CarPlatform::P2_250:
            return "P2x -2004w20 (CAN-HS 250kbit/s)";
        case CarPlatform::P2:
            return "P2x 2004w20- (CAN-HS 500kbit/s)";
        case CarPlatform::P2_UDS:
            return "P28 - V8/SI6";
        case CarPlatform::P3:
            return "Y285/Y286/Y381";
        case CarPlatform::SPA:
            return "EUCD/C1MCA - Generic";
        case CarPlatform::VAG_MED91:
            return "vag_med91";
        case CarPlatform::VAG_MED912:
            return "vag_med912";
        case CarPlatform::Ford_KWP:
            return "ford_kwp";
        case CarPlatform::Ford_UDS:
            return "ford_uds";
        case CarPlatform::Haval_UDS:
            return "haval_uds";
        }
        return {};
    }

    const std::vector<ConfigurationInfo>& staticConfiguration()
    {
        static const std::vector<ConfigurationInfo> configuration(loadConfiguration(CommonData::commonConfiguration));
        return configuration;
    }

    ConfigurationInfo getConfigurationInfoByCarPlatform(CarPlatform carPlatform)
    {
        const auto& configurationInfo{staticConfiguration()};
        const auto platformName = getCarPlatformName(carPlatform);
        const auto confIt = std::find_if(configurationInfo.cbegin(), configurationInfo.cend(), [&platformName](const ConfigurationInfo& info) {
            return info.name == platformName;
        });
        if (confIt == configurationInfo.cend()) {
            throw std::runtime_error("Unknown platform " + platformName);
        }
        return *confIt;
    }

    std::tuple<BusConfiguration, ECUInfo> getEcuInfoByEcuId(CarPlatform carPlatform, uint32_t ecuId)
    {
        const auto conf = getConfigurationInfoByCarPlatform(carPlatform);
        for (const auto& busInfo : conf.busInfo) {
            for (const auto& ecuInfo : busInfo.ecuInfo) {
                if (ecuInfo.ecuId == ecuId) {
                    return { busInfo, ecuInfo };
                }
            }
        }
        const auto platformName = getCarPlatformName(carPlatform);
        throw std::runtime_error((std::stringstream() << "Can'f find ECU with id = " << ecuId + ", for platform = " << platformName).str());
    }

    size_t getChannelIndexByEcuId(CarPlatform carPlatform, uint32_t ecuId,
        const std::vector<std::unique_ptr<j2534::J2534Channel>>& channels)
    {
        const auto [busInfo, ecuInfo] = getEcuInfoByEcuId(carPlatform, ecuId);
        for(size_t i = 0; i < channels.size(); ++i) {
            if (busInfo.baudrate == channels[i]->getBaudrate()) {
                return i;
            }
        }
        throw std::runtime_error((std::stringstream() << "Can'f find opened channel with baudrate = " << busInfo.baudrate).str());
    }

    j2534::J2534Channel& getChannelByEcuId(CarPlatform carPlatform, uint32_t ecuId,
        const std::vector<std::unique_ptr<j2534::J2534Channel>>& channels)
    {
        return *channels[getChannelIndexByEcuId(carPlatform, ecuId, channels)];
    }

    static std::string getNonEmptyHexIntString(const std::string& input)
    {
        return input.empty() ? "0" : input;
    }

    static CompressionType getEcuCompression(const YAML::Node& node)
    {
        const std::string tag = "CompressionType";
        if(!node[tag].IsDefined()) {
            return CompressionType::None;
        }
        const auto compressionType = toLower(node[tag].as<std::string>());
        if(compressionType == "bosch") {
            return CompressionType::Bosch;
        }
        else if(compressionType == "lzss") {
            return CompressionType::LZSS;
        }
        return CompressionType::None;
    }

    static EncryptionType getEcuEncryption(const YAML::Node& node)
    {
        const std::string tag = "EncryptionType";
        if(!node[tag].IsDefined()) {
            return EncryptionType::None;
        }
        const auto compressionType = toLower(node[tag].as<std::string>());
        if(compressionType == "xor") {
            return EncryptionType::XOR;
        }
        else if(compressionType == "aes") {
            return EncryptionType::AES;
        }
        return EncryptionType::None;
    }

    static ECUInfo processEcuNode(const YAML::Node& node)
    {
        ECUInfo ecuInfo;
        ecuInfo.name = node["Name"].as<std::string>();
        ecuInfo.ecuId = std::stoi(node["Address"].as<std::string>(), 0, 16);
        ecuInfo.canId = std::stoi(getNonEmptyHexIntString(node["CANIdentifier"].as<std::string>("")), 0, 16);
        ecuInfo.compressionType = getEcuCompression(node);
        ecuInfo.encryptionType = getEcuEncryption(node);
        return ecuInfo;
    }

    static uint32_t getCanProtocol(const std::string& input)
    {
        if (input == "CAN")
            return CAN;
        else if (input == "15765-2")
            return ISO15765;
        else if (input == "14230-3")
            return ISO14230;
        return CAN;
    }

    static std::vector<ConfigurationInfo> loadConfigurationImpl(const YAML::Node& node)
    {
        std::vector<ConfigurationInfo> result;
        auto confNodes = node["Configuration"];
        for (const auto& confNode : confNodes) {
            ConfigurationInfo info;
            info.name = confNode["Name"].as<std::string>();
            for (const auto& bus : confNode["Bus"]) {
                BusConfiguration busConf;
                busConf.baudrate = bus["BaudRate"].as<uint32_t>() * 1000;
                busConf.canIdBitSize = bus["CANIdBitSize"].as<uint32_t>();
                busConf.protocolId = getCanProtocol(bus["SWDLProtocol"].as<std::string>());
                busConf.name = bus["Name"].as<std::string>();
                const auto& nodes = bus["Node"];
                if (nodes.IsDefined()) {
                    if (nodes.IsSequence()) {
                        for (const auto& node : bus["Node"]) {
                            busConf.ecuInfo.emplace_back(processEcuNode(node));
                        }
                    }
                    else {
                        busConf.ecuInfo.emplace_back(processEcuNode(nodes));
                    }
                }
                info.busInfo.emplace_back(std::move(busConf));
            }
            result.emplace_back(std::move(info));
        }
        return result;
    }

    std::vector<ConfigurationInfo> loadConfiguration(std::istream& input)
    {
        return loadConfigurationImpl(YAML::Load(input));
    }

    std::vector<ConfigurationInfo> loadConfiguration(const std::string& input)
    {
        return loadConfigurationImpl(YAML::Load(input));
    }

    void checkTP20Error(uint8_t requestId, const uint8_t* data, size_t dataSize)
    {
        if (dataSize < 3 || data[0] != 0x7F || data[1] != requestId) {
            return;
        }
        throw TP20Error(data[2]);
    }

    void checkUDSError(uint8_t requestId, const uint8_t* data, size_t dataSize)
    {
        if(dataSize < 7 || data[4] != 0x7F || data[5] != requestId) {
            return;
        }
        throw UDSError(data[6]);
    }

    void checkD2Error(uint8_t ecuId, const std::vector<uint8_t>& requestId, const uint8_t* data, size_t dataSize)
    {
        if(dataSize < 7 || data[4] != 0x7F || data[5] != ecuId) {
            return;
        }
        throw D2Error(data[6]);
    }

    CarPlatform parsePlatform(std::string input)
    {
        input = toLower(input);
        if ("p1" == input)
            return common::CarPlatform::P1;
        else if ("p1_uds" == input)
            return common::CarPlatform::P1_UDS;
        else if ("p2" == input)
            return common::CarPlatform::P2;
        else if ("p2_250" == input)
            return common::CarPlatform::P2_250;
        else if ("p2_uds" == input)
            return common::CarPlatform::P2_UDS;
        else if ("p3" == input)
            return common::CarPlatform::P3;
        else if ("spa" == input)
            return common::CarPlatform::SPA;
        else if ("ford_kwp" == input)
            return common::CarPlatform::Ford_KWP;
        else if ("ford_uds" == input)
            return common::CarPlatform::Ford_UDS;
        else if ("haval_uds" == input)
            return common::CarPlatform::Haval_UDS;
        return common::CarPlatform::Undefined;
    }

    std::array<uint8_t, 5> getPinArray(uint64_t pin)
    {
        return { (pin >> 32) & 0xFF, (pin >> 24) & 0xFF, (pin >> 16) & 0xFF, (pin >> 8) & 0xFF, pin & 0xFF };
    }

    void initLogger(const std::string& logFilename)
    {
        el::Configurations defaultConf;
        defaultConf.setToDefault();
        defaultConf.set(el::Level::Global,
                        el::ConfigurationType::Format, "%datetime %level %msg");
        defaultConf.set(el::Level::Global,
                        el::ConfigurationType::Filename, logFilename);
        el::Loggers::reconfigureLogger("default", defaultConf);
    }

    uint16_t crc16(const uint8_t* data_p, size_t length)
    {
        uint16_t crc = 0xFFFF;

        while (length--) {
            uint8_t x = crc >> 8 ^ *data_p++;
            x ^= x >> 4;
            crc = (crc << 8) ^ ((uint16_t)(x << 12)) ^ ((uint16_t)(x << 5)) ^ ((uint16_t)x);
        }
        return crc;
    }

} // namespace common
