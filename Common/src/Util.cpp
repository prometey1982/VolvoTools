#include "common/Util.hpp"

#include "common/BusConfiguration.hpp"
#include "common/ECUInfo.hpp"

#include <Registry/include/Registry.hpp>
#include <j2534/J2534Channel.hpp>
#include <j2534/J2534_v0404.h>

#include <yaml-cpp/yaml.h>

#include <algorithm>
#include <codecvt>
#include <locale>
#include <unordered_map>

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

    uint32_t encode(uint8_t byte1, uint8_t byte2, uint8_t byte3, uint8_t byte4) {
        return byte1 + (byte2 << 8) + (byte3 << 16) + (byte4 << 24);
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

    std::unique_ptr<j2534::J2534Channel>
        openChannel(j2534::J2534& j2534, unsigned long ProtocolID, unsigned long Flags,
            unsigned long Baudrate, bool AdditionalConfiguration) {
        auto channel{ std::make_unique<j2534::J2534Channel>(j2534, ProtocolID, Flags,
                                                           Baudrate, Flags) };
        std::vector<SCONFIG> config(3);
        config[0].Parameter = DATA_RATE;
        config[0].Value = Baudrate;
        config[1].Parameter = LOOPBACK;
        config[1].Value = 0;
        config[2].Parameter = BIT_SAMPLE_POINT;
        config[2].Value = (Baudrate == 500000 ? 80 : 68);
        channel->setConfig(config);

        unsigned long msgId;
        if (ProtocolID == ISO15765) {
            PASSTHRU_MSG maskMsg =
                makePassThruMsg(ProtocolID, Flags, { 0xFF, 0xFF, 0xFF, 0xFF });
            PASSTHRU_MSG patternMsg =
                makePassThruMsg(ProtocolID, Flags, { 0x00, 0x00, 0x07, 0xE8 });
            PASSTHRU_MSG flowMsg =
                makePassThruMsg(ProtocolID, Flags, { 0x00, 0x00, 0x07, 0xE0 });
            channel->startMsgFilter(FLOW_CONTROL_FILTER, &maskMsg, &patternMsg, &flowMsg, msgId);
        }
        else {
            PASSTHRU_MSG msgFilter =
                makePassThruMsg(ProtocolID, Flags, { 0x00, 0x00, 0x00, 0x01 });
            channel->startMsgFilter(PASS_FILTER, &msgFilter, &msgFilter, nullptr, msgId);
        }
        if (AdditionalConfiguration && ProtocolID == CAN_XON_XOFF) {
            startXonXoffMessageFiltering(*channel, Flags);
            config.resize(1);
            config[0].Parameter = CAN_XON_XOFF;
            config[0].Value = 0;
            channel->setConfig(config);
        }
        return std::move(channel);
    }

    std::unique_ptr<j2534::J2534Channel>
        openUDSChannel(j2534::J2534& j2534, unsigned long Baudrate, uint32_t cmId) {
        const unsigned long protocolId = ISO15765;
        const unsigned long flags = ISO15765_FRAME_PAD;
        auto channel{ std::make_unique<j2534::J2534Channel>(j2534, protocolId, 0,
                                                           Baudrate, 0) };
        std::vector<SCONFIG> config(3);
        config[0].Parameter = DATA_RATE;
        config[0].Value = Baudrate;
        config[1].Parameter = LOOPBACK;
        config[1].Value = 0;
        config[2].Parameter = BIT_SAMPLE_POINT;
        config[2].Value = (Baudrate == 500000 ? 80 : 68);
        channel->setConfig(config);

        unsigned long msgId;
        PASSTHRU_MSG maskMsg =
            makePassThruMsg(protocolId, flags, { 0xFF, 0xFF, 0xFF, 0xFF });
        PASSTHRU_MSG patternMsg =
            makePassThruMsg(protocolId, flags, { 0x00, 0x00, 0x07, 0xE8 });
        PASSTHRU_MSG flowMsg =
            makePassThruMsg(protocolId, flags, { 0x00, 0x00, 0x07, 0xE0 });
        channel->startMsgFilter(FLOW_CONTROL_FILTER, &maskMsg, &patternMsg, &flowMsg, msgId);

        return std::move(channel);
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
            std::vector<SCONFIG> config(3);
            config[0].Parameter = DATA_RATE;
            config[0].Value = Baudrate;
            config[1].Parameter = LOOPBACK;
            config[1].Value = 0;
            config[2].Parameter = BIT_SAMPLE_POINT;
            config[2].Value = (Baudrate == 500000 ? 80 : 68);
            channel->setConfig(config);

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

    std::vector<uint8_t> readMessageSequence(j2534::J2534Channel& channel,
        size_t queryLength) {
        const size_t MaxErrorCount = 10;
        size_t errorCount = 0;
        std::vector<uint8_t> result;
        for (bool inSeries = false, firstRun = true; inSeries || firstRun;
            firstRun = false) {
            std::vector<PASSTHRU_MSG> msgs(1);
            if (channel.readMsgs(msgs) != STATUS_NOERROR) {
                ++errorCount;
            }
            if (errorCount >= MaxErrorCount) {
                throw std::runtime_error("Reading ECU failed");
            }
            for (const auto& msg : msgs) {
                auto offset = 5u + queryLength;
                auto count = 12u;
                auto messageType = msg.Data[4];
                if (messageType == 0x8f) { // begin of the series
                    inSeries = true;
                    count = count > offset ? count - offset : 0;
                }
                else if (messageType == 0x09) { // second message
                    offset = 5;
                    count = 7;
                }
                else if ((messageType & 0x40) == 0) { // in the middle
                    offset = 5;
                    count = 7;
                }
                else { // end of the series
                    offset = 5;
                    count = messageType - 0x48;
                    inSeries = false;
                }
                for (unsigned long j = 0; j < count; ++j) {
                    result.push_back(msg.Data[offset + j]);
                }
            }
        }
        return result;
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
            if (channel.readMsgs(read_msgs, 1000) != STATUS_NOERROR || read_msgs.empty())
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
            case 'L':
                result = CarPlatform::P80;
                break;
            case 'T':
            case 'R':
            case 'S':
            case 'С':
                result = CarPlatform::P2;
                break;
            case 'M':
                result = CarPlatform::P1;
                break;
            }
        }
        return result;
    }

    j2534::J2534Channel& getChannelByEcuId(uint32_t ecuId, const std::vector<std::unique_ptr<j2534::J2534Channel>>& channels)
    {
        static const std::unordered_map<uint32_t, size_t> CMMap = {
            {0x10, 0},
            {0x7A, 0},
            {0x6E, 0},
        };
        return *channels[CMMap.at(ecuId)];
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
        }
        return {};
    }

    j2534::J2534Channel& getChannelByEcuId(const std::vector<ConfigurationInfo>& configurationInfo, CarPlatform carPlatform, uint32_t cmId,
        const std::vector<std::unique_ptr<j2534::J2534Channel>>& channels)
    {
        const auto platformName = getCarPlatformName(carPlatform);
        const auto confIt = std::find_if(configurationInfo.cbegin(), configurationInfo.cend(), [&platformName](const ConfigurationInfo& info) {
            return info.name == platformName;
            });
        if (confIt == configurationInfo.cend()) {
            throw std::runtime_error("Unknown platform " + platformName);
        }
        for (const auto& busInfo : confIt->busInfo) {
            for (const auto& ecuInfo : busInfo.ecuInfo) {
                if (ecuInfo.ecuId == cmId) {
                    for (const auto& channel : channels) {
                        if (busInfo.baudrate == channel->getBaudrate()) {
                            return *channel;
                        }
                    }
                    throw std::runtime_error((std::stringstream() << "Can'f find opened channel with baudrate = " << busInfo.baudrate).str());
                }
            }
        }
        throw std::runtime_error((std::stringstream() << "Can'f find ECU with id = " << cmId + ", for platform = " << platformName).str());
    }

    static std::string getNonEmptyHexIntString(const std::string& input)
    {
        return input.empty() ? "0" : input;
    }

    static ECUInfo processEcuNode(const YAML::Node& node)
    {
        ECUInfo ecuInfo;
        ecuInfo.name = node["Name"].as<std::string>();
        ecuInfo.ecuId = std::stoi(node["Address"].as<std::string>(), 0, 16);
        ecuInfo.canId = std::stoi(getNonEmptyHexIntString(node["CANIdentifier"].as<std::string>("")), 0, 16);
        return ecuInfo;
    }

    static uint32_t getCanProtocol(const std::string& input)
    {
        if (input == "CAN")
            return CAN;
        else if (input == "15765-2")
            return ISO15765;
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

} // namespace common
