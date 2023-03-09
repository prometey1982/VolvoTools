#include "Logger.hpp"

#include "../Common/D2Message.hpp"
#include "../Common/D2Messages.hpp"
#include "../Common/Util.hpp"
#include "../j2534/J2534.hpp"
#include "../j2534/J2534Channel.hpp"
#include "LoggerCallback.hpp"

#include <algorithm>
#include <chrono>
#include <fstream>
#include <numeric>

namespace logger {

class LoggerImpl {
public:
  LoggerImpl(j2534::J2534 &j2534) : _j2534(j2534) {}

  virtual void registerParameters(j2534::J2534Channel &channel,
                                  const LogParameters &parameters) = 0;
  virtual std::vector<uint32_t>
  requestMemory(j2534::J2534Channel &channel,
                const LogParameters &parameters) = 0;

protected:
  j2534::J2534 &_j2534;
};

class D2LoggerImpl : public LoggerImpl {
public:
  D2LoggerImpl(j2534::J2534 &j2534) : LoggerImpl(j2534) {}

private:
  const unsigned long protocolId = CAN;
  const unsigned long flags = CAN_29BIT_ID;

  const std::vector<PASSTHRU_MSG> requstMemoryMessage{
      common::D2Messages::requestMemory.toPassThruMsgs(protocolId, flags)};

  unsigned long getNumberOfCanMessages(const LogParameters& parameters) const {
      double totalDataLength =
          std::accumulate(parameters.parameters().cbegin(), parameters.parameters().cend(), 0,
              [](size_t prevValue, const auto& param) {
                  return prevValue + param.size();
              });
      return static_cast<unsigned long>(std::ceil((totalDataLength - 3) / 7)) + 1;
  }

  virtual void registerParameters(j2534::J2534Channel &channel,
                                  const LogParameters &parameters) override {
    unsigned long numMsgs;
    channel.writeMsgs(
        common::D2Messages::unregisterAllMemoryRequest.toPassThruMsgs(
            protocolId, flags),
        numMsgs);
    std::vector<PASSTHRU_MSG> result(1);
    channel.readMsgs(result);
    for (const auto parameter : parameters.parameters()) {
      const auto registerParameterRequest{
          common::D2Messages::makeRegisterAddrRequest(parameter.addr(),
                                                      parameter.size())};
      unsigned long numMsgs;
      channel.writeMsgs(
          registerParameterRequest.toPassThruMsgs(protocolId, flags), numMsgs);
      if (numMsgs == 0) {
        throw std::runtime_error("Request to the ECU wasn't send");
      }
      result.resize(1);
      channel.readMsgs(result);
      if (result.empty()) {
        throw std::runtime_error("ECU didn't response intime");
      }
      for (const auto msg : result) {
        if (msg.Data[6] != 0xEA)
          throw std::runtime_error("Can't register memory addr");
      }
    }
  }

  virtual std::vector<uint32_t>
  requestMemory(j2534::J2534Channel &channel,
                const LogParameters &parameters) override {
    const auto numberOfCanMessages = getNumberOfCanMessages(parameters);
    std::vector<uint32_t> result;
    unsigned long writtenCount = 1;
    channel.writeMsgs(requstMemoryMessage, writtenCount);
    if (writtenCount > 0) {
      std::vector<PASSTHRU_MSG> logMessages(numberOfCanMessages);
      channel.readMsgs(logMessages);

      result.reserve(parameters.parameters().size());

      size_t paramIndex = 0;
      size_t paramOffset = 0;
      uint16_t value = 0;
      for (const auto &msg : logMessages) {
        size_t msgOffset = 5;
        // E6 F0 00 - read record by identifier answer
        if (msg.Data[4] == 0x8F &&
            msg.Data[5] == static_cast<uint8_t>(common::ECUType::ECM_ME) &&
            msg.Data[6] == 0xE6 && msg.Data[7] == 0xF0 && msg.Data[8] == 0)
          msgOffset = 9;
        for (size_t i = msgOffset; i < 12; ++i) {
          const auto &param = parameters.parameters()[paramIndex];
          value += msg.Data[i] << ((param.size() - paramOffset - 1) * 8);
          ++paramOffset;
          if (paramOffset >= param.size()) {
            result.push_back(value);
            ++paramIndex;
            paramOffset = 0;
            value = 0;
          }
          if (paramIndex >= parameters.parameters().size())
            break;
        }
      }
    }
    return result;
  }
};

class UDSLoggerImpl : public LoggerImpl {
public:
  UDSLoggerImpl(j2534::J2534 &j2534) : LoggerImpl(j2534) {}

private:
  virtual void
  registerParameters(j2534::J2534Channel & /*channel*/,
                     const LogParameters & /*parameters*/) override {}
  virtual std::vector<uint32_t>
  requestMemory(j2534::J2534Channel & /*channel*/,
                const LogParameters & /*parameters*/) override {
    std::vector<uint32_t> result;
    return result;
  }
};

std::unique_ptr<LoggerImpl> createLoggerImpl(LoggerType loggerType,
                                             j2534::J2534 &j2534) {
  switch (loggerType) {
  case LoggerType::LT_D2:
    return std::make_unique<D2LoggerImpl>(j2534);
  case LoggerType::LT_UDS:
    return std::make_unique<UDSLoggerImpl>(j2534);
  }
  throw std::runtime_error("Not implemented");
}

Logger::Logger(j2534::J2534 &j2534, LoggerType loggerType)
    : _j2534{j2534}, _loggingThread{}, _stopped{true},
      _loggerImpl(createLoggerImpl(loggerType, j2534)) {}

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

  //  const unsigned long protocolId = CAN_XON_XOFF;
  const unsigned long protocolId = CAN;
  const unsigned long flags = CAN_29BIT_ID;

  _channel1 = common::openChannel(_j2534, protocolId, flags, baudrate);
  //		_channel2 = openChannel(protocolId, flags | 0x20000000, 125000);
  if (baudrate != 500000)
    _channel3 = common::openBridgeChannel(_j2534);

  registerParameters();

  _stopped = false;

  _callbackThread = std::thread([this]() { callbackFunction(); });

  _loggingThread = std::thread([this, protocolId, flags]() { logFunction(); });
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

void Logger::registerParameters() {
  _loggerImpl->registerParameters(*_channel1, _parameters);
}

void Logger::logFunction() {
  {
    std::unique_lock<std::mutex> lock{_callbackMutex};
    for (const auto callback : _callbacks) {
      callback->onStatusChanged(true);
    }
  }
  const auto startTimepoint{std::chrono::steady_clock::now()};
  for (size_t timeoffset = 0;; timeoffset += 50) {
    {
      std::unique_lock<std::mutex> lock{_mutex};
      if (_stopped)
        break;
    }
    _channel1->clearRx();
    _channel1->clearTx();
    auto logRecord = _loggerImpl->requestMemory(*_channel1, _parameters);
    const auto now{std::chrono::steady_clock::now()};
    pushRecord(LogRecord(std::chrono::duration_cast<std::chrono::milliseconds>(
                             now - startTimepoint),
                         std::move(logRecord)));
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
