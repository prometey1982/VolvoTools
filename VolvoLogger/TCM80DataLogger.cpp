#include "TCM80DataLogger.hpp"

#include "../Common/D2Message.hpp"
#include "../Common/D2Messages.hpp"
#include "../Common/Util.hpp"
#include "../j2534/J2534.hpp"
#include "../j2534/J2534Channel.hpp"
#include "LoggerCallback.hpp"

#include <algorithm>
#include <chrono>
#include <fstream>

namespace logger {

TCM80DataLogger::TCM80DataLogger(j2534::J2534 &j2534)
    : _j2534{j2534}, _loggingThread{}, _stopped{true} {}

TCM80DataLogger::~TCM80DataLogger() { stop(); }

void TCM80DataLogger::registerCallback(LoggerCallback &callback) {
  std::unique_lock<std::mutex> lock{_callbackMutex};
  if (std::find(_callbacks.cbegin(), _callbacks.cend(), &callback) ==
      _callbacks.cend()) {
    _callbacks.push_back(&callback);
  }
}

void TCM80DataLogger::unregisterCallback(LoggerCallback &callback) {
  std::unique_lock<std::mutex> lock{_callbackMutex};
  _callbacks.erase(std::remove(_callbacks.begin(), _callbacks.end(), &callback),
                   _callbacks.end());
}

void TCM80DataLogger::start(unsigned long baudrate,
                            const LogParameters &parameters) {
  std::unique_lock<std::mutex> lock{_mutex};
  if (!_stopped) {
    throw std::runtime_error("Logging already started");
  }

  _parameters = parameters;

  const unsigned long protocolId = CAN;
  const unsigned long flags = CAN_29BIT_ID;

  _channel1 = common::openChannel(_j2534, protocolId, flags, baudrate);
  _channel3 = common::openBridgeChannel(_j2534);

  _stopped = false;

  _callbackThread = std::thread([this]() { callbackFunction(); });

  _loggingThread = std::thread(
      [this, protocolId, flags]() { logFunction(protocolId, flags); });
}

void TCM80DataLogger::stop() {
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
  _channel3.reset();
}

void TCM80DataLogger::logFunction(unsigned long protocolId,
                                  unsigned int flags) {
  {
    std::unique_lock<std::mutex> lock{_callbackMutex};
    for (const auto callback : _callbacks) {
      callback->onStatusChanged(true);
    }
  }
  std::vector<uint32_t> logRecord(_parameters.parameters().size());
  std::vector<PASSTHRU_MSG> logMessages(1);
  const auto startTimepoint{std::chrono::steady_clock::now()};
  for (;;) {
    logRecord.resize(_parameters.parameters().size());
    {
      std::unique_lock<std::mutex> lock{_mutex};
      if (_stopped)
        break;
    }
    const auto now{std::chrono::steady_clock::now()};
    for (size_t i = 0; i < _parameters.parameters().size(); ++i) {
      auto message = common::D2Messages::createReadTCMDataByAddr(
          _parameters.parameters()[i].addr(),
          _parameters.parameters()[i].size());

      unsigned long writtenCount = 1;
      const auto write_msgs = message.toPassThruMsgs(protocolId, flags);
      for(size_t j = 0; j < write_msgs.size(); ++j) {
          std::this_thread::sleep_for(std::chrono::milliseconds(3));
          _channel1->writeMsgs({write_msgs[j]},
                               writtenCount);
          if(!writtenCount)
              break;
      }
      if (writtenCount > 0) {
        logMessages.resize(2);
        _channel1->readMsgs(logMessages);
        if (logMessages.size() >= 2) {
          auto logMessage = logMessages[1];
          const auto &data = logMessage.Data;
          size_t msgOffset = 6;
          if (_parameters.parameters()[i].size() == 1)
            logRecord[i] = common::encode(data[msgOffset]);
          else if (_parameters.parameters()[i].size() == 2)
            logRecord[i] = common::encode(data[msgOffset + 1], data[msgOffset]);
          else if (_parameters.parameters()[i].size() == 3)
            logRecord[i] = common::encode(data[msgOffset + 2],
                                          data[msgOffset + 1], data[msgOffset]);
          else if (_parameters.parameters()[i].size() == 4)
            logRecord[i] =
                common::encode(data[msgOffset + 3], data[msgOffset + 2],
                               data[msgOffset + 1], data[msgOffset]);
        }
      }
    }
    if (!logRecord.empty())
      pushRecord(
          LogRecord(std::chrono::duration_cast<std::chrono::milliseconds>(
                        now - startTimepoint),
                    std::move(logRecord)));
  }
  {
    std::unique_lock<std::mutex> lock{_callbackMutex};
    for (const auto callback : _callbacks) {
      callback->onStatusChanged(false);
    }
  }
}

void TCM80DataLogger::pushRecord(TCM80DataLogger::LogRecord &&record) {
  std::unique_lock<std::mutex> lock{_callbackMutex};
  _loggedRecords.emplace_back(std::move(record));
  _callbackCond.notify_all();
}

void TCM80DataLogger::callbackFunction() {
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
    for (size_t i = 0;
         i < logRecord.values.size() && i < _parameters.parameters().size();
         ++i) {
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
