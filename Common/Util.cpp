#include "Util.hpp"

#include "../Registry/Registry/include/Registry.hpp"
#include "../j2534/J2534Channel.hpp"
#include "../j2534/J2534_v0404.h"

#include <codecvt>
#include <locale>

namespace common {

std::wstring toWstring(const std::string &str) {
  using convert_type = std::codecvt_utf8<wchar_t>;
  std::wstring_convert<convert_type, wchar_t> converter;
  return converter.from_bytes(str);
}

std::string toString(const std::wstring &str) {
  using convert_type = std::codecvt_utf8<wchar_t>;
  std::wstring_convert<convert_type, wchar_t> converter;
  return converter.to_bytes(str);
}

#ifdef UNICODE
std::wstring toPlatformString(const std::string &str) { return toWstring(str); }

std::string fromPlatformString(const std::wstring &str) {
  return toString(str);
}
#else
std::string toPlatformString(const std::string &str) { return str; }

std::string fromPlatformString(const std::string &str) { return str; }
#endif

using namespace m4x1m1l14n::Registry;

static bool processRegistry(const std::string &keyName,
                            std::string &libraryPath, std::string &deviceName) {
  const auto key = LocalMachine->Open(toPlatformString(keyName));
  try {
    auto localLibraryPath = fromPlatformString(key->GetString(TEXT("FunctionLibrary")));
    if (localLibraryPath.find("TSDiCE32.dll") != std::string::npos) {
        libraryPath = localLibraryPath;
        deviceName = fromPlatformString(key->GetString(TEXT("Name")));
    }
  } catch (...) {
    return false;
  }
  return !libraryPath.empty();
}

std::pair<std::string, std::string> getLibraryParams() {
  std::string libraryPath;
  std::string deviceName;
  const std::string rootKeyName{"Software\\PassThruSupport.04.04"};
  const auto key = LocalMachine->Open(toPlatformString(rootKeyName));
  key->EnumerateSubKeys([&rootKeyName, &libraryPath,
                         &deviceName](const auto &subKeyName) {
    return processRegistry(rootKeyName + "\\" + fromPlatformString(subKeyName),
                           libraryPath, deviceName);
  });
  return {libraryPath, deviceName};
}

static PASSTHRU_MSG makePassThruMsg(unsigned long ProtocolID,
                                    unsigned long Flags,
                                    const std::vector<unsigned char> &data) {
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
                 const std::vector<std::vector<unsigned char>> &data) {
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

static void startXonXoffMessageFiltering(j2534::J2534Channel &channel,
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
                       })};

  channel.passThruIoctl(CAN_XON_XOFF_FILTER, msgs.data());
  unsigned long msgId = 0;
  channel.passThruIoctl(CAN_XON_XOFF_FILTER_ACTIVE, &msgId);
}

std::unique_ptr<j2534::J2534Channel> openChannel(j2534::J2534 &j2534,
                                                 unsigned long ProtocolID,
                                                 unsigned long Flags,
                                                 unsigned long Baudrate) {
  auto channel{std::make_unique<j2534::J2534Channel>(j2534, ProtocolID, Flags,
                                                     Baudrate)};
  std::vector<SCONFIG> config(3);
  config[0].Parameter = DATA_RATE;
  config[0].Value = Baudrate;
  config[1].Parameter = LOOPBACK;
  config[1].Value = 0;
  config[2].Parameter = BIT_SAMPLE_POINT;
  config[2].Value = (Baudrate == 500000 ? 80 : 68);
  channel->setConfig(config);

  PASSTHRU_MSG msgFilter =
      makePassThruMsg(ProtocolID, Flags, {0x00, 0x00, 0x00, 0x01});
  unsigned long msgId;
  channel->startMsgFilter(PASS_FILTER, &msgFilter, &msgFilter, nullptr, msgId);
  startXonXoffMessageFiltering(*channel, Flags);
  config.resize(1);
  config[0].Parameter = CAN_XON_XOFF;
  config[0].Value = 0;
  channel->setConfig(config);
  return std::move(channel);
}

std::unique_ptr<j2534::J2534Channel> openBridgeChannel(j2534::J2534 &j2534) {
  const unsigned long ProtocolId = ISO9141;
  const unsigned long Flags = ISO9141_K_LINE_ONLY;
  auto channel{
      std::make_unique<j2534::J2534Channel>(j2534, ProtocolId, Flags, 10400)};
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
      makePassThruMsg(ProtocolId, Flags, {0x84, 0x40, 0x13, 0xb2, 0xf0, 0x03});
  unsigned long msgId;
  channel->startPeriodicMsg(msg, msgId, 2000);

  return std::move(channel);
}

} // namespace common
