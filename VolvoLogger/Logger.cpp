#include "Logger.h"

#include "../Common/CEMCanMessage.hpp"
#include "../Common/CanMessages.hpp"
#include "../Common/Util.hpp"
#include "../j2534/J2534.hpp"
#include "../j2534/J2534Channel.hpp"
#include "LoggerCallback.hpp"

#include <algorithm>
#include <chrono>
#include <fstream>

namespace logger {

Logger::Logger(j2534::J2534 &j2534)
    : _j2534{j2534}, _loggingThread{}, _stopped{true} {}

Logger::~Logger() { stop(); }

void Logger::registerCallback(LoggerCallback &callback) {
  std::unique_lock<std::mutex> lock{_callbackMutex};
  if (std::find(_callbacks.cbegin(), _callbacks.cend(), &callback) ==
      _callbacks.cend()) {
    _callbacks.push_back(&callback);
  }
}

void Logger::unregisterCallback(LoggerCallback &callback) {
  std::unique_lock<std::mutex> lock{_callbackMutex};
  _callbacks.erase(std::remove(_callbacks.begin(), _callbacks.end(), &callback),
                   _callbacks.end());
}

void Logger::start(unsigned long baudrate, const LogParameters &parameters) {
  std::unique_lock<std::mutex> lock{_mutex};
  if (!_stopped) {
    throw std::runtime_error("Logging already started");
  }

  _parameters = parameters;

  const unsigned long protocolId = CAN_XON_XOFF;
  const unsigned long flags = CAN_29BIT_ID;

  _channel1 = common::openChannel(_j2534, protocolId, flags, baudrate);
  //		_channel2 = openChannel(protocolId, flags | 0x20000000, 125000);
  if (baudrate != 500000)
    _channel3 = common::openBridgeChannel(_j2534);

  registerParameters(protocolId, flags);

  _stopped = false;

  _callbackThread = std::thread([this]() { callbackFunction(); });

  _loggingThread = std::thread(
      [this, protocolId, flags]() { logFunction(protocolId, flags); });
}

void Logger::stop() {
  {
    std::unique_lock<std::mutex> lock{_mutex};
    _stopped = true;
  }
  if (_loggingThread.joinable())
    _loggingThread.join();

  {
    std::unique_lock<std::mutex> lock{_callbackMutex};
    _callbackCond.notify_all();
  }

  if (_callbackThread.joinable())
    _callbackThread.join();

  _channel1.reset();
  //		_channel2.reset();
  _channel3.reset();
}

void Logger::registerParameters(unsigned long ProtocolID, unsigned long Flags) {
  for (const auto parameter : _parameters.parameters()) {
    const auto registerParameterRequest{
        common::CanMessages::makeRegisterAddrRequest(parameter.addr(),
                                                     parameter.size())};
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

void Logger::logFunction(unsigned long protocolId, unsigned int flags) {
  {
    std::unique_lock<std::mutex> lock{_callbackMutex};
    for (const auto callback : _callbacks) {
      callback->onStatusChanged(true);
    }
  }
  const auto startTimepoint{std::chrono::steady_clock::now()};
  unsigned long numberOfCanMessages{0};
  {
    std::unique_lock<std::mutex> lock{_mutex};
    numberOfCanMessages = _parameters.getNumberOfCanMessages();
  }
  std::vector<PASSTHRU_MSG> logMessages(numberOfCanMessages);
  const std::vector<PASSTHRU_MSG> requstMemoryMessage{
      common::CanMessages::requestMemory.toPassThruMsg(protocolId, flags)};
  for (size_t timeoffset = 0;; timeoffset += 20) {
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

      std::vector<uint32_t> logRecord;
      logRecord.reserve(_parameters.parameters().size());

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
            logRecord.push_back(value);
            ++paramIndex;
            paramOffset = 0;
            value = 0;
          }
          if (paramIndex >= _parameters.parameters().size())
            break;
        }
      }
      pushRecord(
          LogRecord(std::chrono::duration_cast<std::chrono::milliseconds>(
                        now - startTimepoint),
                    std::move(logRecord)));
    }
    std::unique_lock<std::mutex> lock{_mutex};
    _cond.wait_until(lock,
                     startTimepoint + std::chrono::milliseconds(timeoffset));
  }
  {
    std::unique_lock<std::mutex> lock{_callbackMutex};
    for (const auto callback : _callbacks) {
      callback->onStatusChanged(false);
    }
  }
}

void Logger::pushRecord(Logger::LogRecord &&record) {
  std::unique_lock<std::mutex> lock{_callbackMutex};
  _loggedRecords.emplace_back(std::move(record));
  _callbackCond.notify_all();
}

void Logger::callbackFunction() {
  for (;;) {
    LogRecord logRecord;
    {
      std::unique_lock<std::mutex> lock{_callbackMutex};
      _callbackCond.wait(
          lock, [this] { return _stopped || !_loggedRecords.empty(); });
      if (_stopped)
        break;
      logRecord = _loggedRecords.front();
      _loggedRecords.pop_front();
    }
    std::vector<double> formattedValues(logRecord.values.size());
    for (size_t i = 0; i < logRecord.values.size(); ++i) {
      formattedValues[i] =
          _parameters.parameters()[i].formatValue(logRecord.values[i]);
    }
    {
      std::unique_lock<std::mutex> lock{_callbackMutex};
      for (const auto callback : _callbacks) {
        callback->onLogMessage(logRecord.timePoint, formattedValues);
      }
    }
  }
}

} // namespace logger
