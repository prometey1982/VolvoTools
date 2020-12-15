#include "Logger.h"

#include "CEMCanMessage.hpp"
#include "J2534Channel.hpp"

#include <algorithm>
#include <chrono>
#include <fstream>

namespace logger {
enum ECUType { ECM = 0x7A, DEM = 0x1A, TCM = 0xE6 };

static CEMCanMessage makeCanMessage(ECUType ecuType,
                                    std::vector<uint8_t> request) {
  const uint8_t requestLength = 0xC8 + 1 + request.size();
  request.insert(request.begin(), ecuType);
  request.insert(request.begin(), requestLength);
  return CEMCanMessage(request);
}

static CEMCanMessage makeRegisterAddrRequest(uint32_t addr, size_t dataLength) {
  uint8_t byte1 = (addr & 0xFF0000) >> 16;
  uint8_t byte2 = (addr & 0xFF00) >> 8;
  uint8_t byte3 = (addr & 0xFF);
  return makeCanMessage(
      ECM, {0xAA, 0x50, byte1, byte2, byte3, static_cast<uint8_t>(dataLength)});
}

static const CEMCanMessage requestMemory{
    makeCanMessage(ECM, {0xA6, 0xF0, 0x00, 0x01})};
static const CEMCanMessage unregisterAllMemoryRequest{
    makeCanMessage(ECM, {0xAA, 0x00})};

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

Logger::Logger(j2534::J2534 &j2534)
    : _j2534{j2534}, _loggingThread{}, _stopped{true} {}

Logger::~Logger() { stop(); }

void Logger::start(unsigned long baudrate, const LogParameters &parameters,
                   const std::string &savePath) {
  std::unique_lock<std::mutex> lock{_mutex};
  if (!_stopped) {
    throw std::runtime_error("Logging already started");
  }

  _parameters = parameters;

  const unsigned long protocolId = CAN_XON_XOFF;
  const unsigned long flags = CAN_29BIT_ID;

  _channel1 = openChannel(protocolId, flags, baudrate);
  //		_channel2 = openChannel(protocolId, flags | 0x20000000, 125000);
  if (baudrate != 500000)
    _channel3 = openBridgeChannel();

  registerParameters(protocolId, flags);

  _stopped = false;

  _loggingThread = std::thread([this, savePath, protocolId, flags]() {
    logFunction(protocolId, flags, savePath);
  });
}

void Logger::stop() {
  {
    std::unique_lock<std::mutex> lock{_mutex};
    _stopped = true;
  }
  if (_loggingThread.joinable())
    _loggingThread.join();

  _channel1.reset();
  //		_channel2.reset();
  _channel3.reset();
}

std::unique_ptr<j2534::J2534Channel>
Logger::openChannel(unsigned long ProtocolID, unsigned long Flags,
                    unsigned long Baudrate) {
  auto channel{std::make_unique<j2534::J2534Channel>(_j2534, ProtocolID, Flags,
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
  startXonXoffMessageFiltering(*channel, 0);
  return std::move(channel);
}

std::unique_ptr<j2534::J2534Channel> Logger::openBridgeChannel() {
  const unsigned long ProtocolId = ISO9141;
  const unsigned long Flags = ISO9141_K_LINE_ONLY;
  auto channel{
      std::make_unique<j2534::J2534Channel>(_j2534, ProtocolId, Flags, 10400)};
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

void Logger::startXonXoffMessageFiltering(j2534::J2534Channel &channel,
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
  channel.passThruIoctl(CAN_XON_XOFF_FILTER_ACTIVE, nullptr);
}

void Logger::registerParameters(unsigned long ProtocolID, unsigned long Flags) {
  for (const auto parameter : _parameters.parameters()) {
    const auto registerParameterRequest{
        makeRegisterAddrRequest(parameter.addr(), parameter.size())};
    unsigned long numMsgs;
    _channel1->writeMsgs(
        {registerParameterRequest.toPassThruMsg(ProtocolID, Flags)}, numMsgs);
    if (numMsgs == 0) {
      throw std::runtime_error("Request to the ECU wasn't send");
    }
    std::vector<PASSTHRU_MSG> result(1);
    _channel1->readMsgs(result);
    if (result.empty()) {
      throw std::runtime_error("ECU didn't response intime");
    }
    for (const auto msg : result) {
      if (msg.Data[6] != 0xEA)
        throw std::runtime_error("Can't register memory addr");
    }
  }
}

void Logger::logFunction(unsigned long protocolId, unsigned int flags,
                         const std::string &savePath) {
  std::ofstream outputStream(savePath);
  outputStream << "Time (sec),";
  const auto startTimepoint{std::chrono::steady_clock::now()};
  unsigned long numberOfCanMessages{0};
  {
    std::unique_lock<std::mutex> lock{_mutex};
    numberOfCanMessages = _parameters.getNumberOfCanMessages();
    for (const auto &param : _parameters.parameters()) {
      outputStream << param.name() << "(" << param.unit() << "),";
    }
    outputStream << std::endl;
  }
  std::vector<PASSTHRU_MSG> logMessages(numberOfCanMessages);
  const std::vector<PASSTHRU_MSG> requstMemoryMessage{
      requestMemory.toPassThruMsg(protocolId, flags)};
  for (size_t timeoffset = 0;; timeoffset += 50) {
    {
      std::unique_lock<std::mutex> lock{_mutex};
      if (_stopped)
        break;
    }
    unsigned long writtenCount = 1;
    _channel1->writeMsgs(requstMemoryMessage, writtenCount);
    if (writtenCount > 0) {
      logMessages.resize(numberOfCanMessages);
      _channel1->readMsgs(logMessages);

      const auto now{std::chrono::steady_clock::now()};

      outputStream << ((std::chrono::duration_cast<std::chrono::milliseconds>(
                            now - startTimepoint))
                           .count() /
                       1000.0)
                   << ",";

      size_t paramIndex = 0;
      size_t paramOffset = 0;
      uint16_t value = 0;
      for (const auto &msg : logMessages) {
        size_t msgOffset = 5;
        if (msg.Data[4] == 143 && msg.Data[5] == 122 && msg.Data[6] == 230 &&
            msg.Data[7] == 240 && msg.Data[8] == 0)
          msgOffset = 9;
        for (size_t i = msgOffset; i < 12; ++i) {
          const auto &param = _parameters.parameters()[paramIndex];
          value += msg.Data[i] << ((param.size() - paramOffset - 1) * 8);
          ++paramOffset;
          if (paramOffset >= param.size()) {
            outputStream << param.formatValue(value) << ",";
            ++paramIndex;
            paramOffset = 0;
            value = 0;
          }
          if (paramIndex >= _parameters.parameters().size())
            break;
        }
      }
      outputStream << std::endl;
    }
    std::unique_lock<std::mutex> lock{_mutex};
    _cond.wait_until(lock,
                     startTimepoint + std::chrono::milliseconds(timeoffset));
  }
}

} // namespace logger
