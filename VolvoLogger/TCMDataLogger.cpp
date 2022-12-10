#include "TCMDataLogger.hpp"

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

TCMDataLogger::TCMDataLogger(j2534::J2534 &j2534)
    : _j2534{j2534}, _loggingThread{}, _stopped{true} {}

TCMDataLogger::~TCMDataLogger() { stop(); }

void TCMDataLogger::registerCallback(LoggerCallback &callback) {
  std::unique_lock<std::mutex> lock{_callbackMutex};
  if (std::find(_callbacks.cbegin(), _callbacks.cend(), &callback) ==
      _callbacks.cend()) {
    _callbacks.push_back(&callback);
  }
}

void TCMDataLogger::unregisterCallback(LoggerCallback &callback) {
  std::unique_lock<std::mutex> lock{_callbackMutex};
  _callbacks.erase(std::remove(_callbacks.begin(), _callbacks.end(), &callback),
                   _callbacks.end());
}

void TCMDataLogger::start(unsigned long baudrate, const LogParameters &parameters) {
  std::unique_lock<std::mutex> lock{_mutex};
  if (!_stopped) {
    throw std::runtime_error("Logging already started");
  }

  _parameters = parameters;

  const unsigned long protocolId = CAN_XON_XOFF;
  const unsigned long flags = CAN_29BIT_ID;

  _channel1 = common::openChannel(_j2534, protocolId, flags, baudrate);
//  if (baudrate != 500000)
    _channel3 = common::openBridgeChannel(_j2534);

  _stopped = false;

  _callbackThread = std::thread([this]() { callbackFunction(); });

  _loggingThread = std::thread(
      [this, protocolId, flags]() { logFunction(protocolId, flags); });
}

void TCMDataLogger::stop() {
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

void TCMDataLogger::logFunction(unsigned long protocolId, unsigned int flags) {
  {
    std::unique_lock<std::mutex> lock{_callbackMutex};
    for (const auto callback : _callbacks) {
      callback->onStatusChanged(true);
    }
  }
  auto testerPresentMsg = common::CanMessages::enableCommunicationMsg;
  unsigned long testerPresentMsgID;
  _channel1->startPeriodicMsg(testerPresentMsg.toPassThruMsg(protocolId, flags), testerPresentMsgID, 1000);
  std::vector<uint32_t> logRecord(_parameters.parameters().size());
  std::vector<PASSTHRU_MSG> logMessages(1);
  const auto startTimepoint{std::chrono::steady_clock::now()};
  for(;;) {
    logRecord.resize(_parameters.parameters().size());
    {
      std::unique_lock<std::mutex> lock{_mutex};
      if (_stopped)
        break;
    }
    const auto now{std::chrono::steady_clock::now()};
    for(size_t i = 0; i < _parameters.parameters().size(); ++i) {
        auto message = common::CanMessages::createReadTCMDataByAddr(
                    _parameters.parameters()[i].addr(),
                    _parameters.parameters()[i].size());

        unsigned long writtenCount = 1;
        _channel1->writeMsgs(message.toPassThruMsgs(protocolId, flags), writtenCount);
        if (writtenCount > 0) {
            logMessages.resize(2);
            _channel1->readMsgs(logMessages);
            if (logMessages.size() >= 2) {
                auto logMessage = logMessages[1];
                const auto& data = logMessage.Data;
                size_t msgOffset = 6;
                if(_parameters.parameters()[i].size() == 1)
                    logRecord[i] = common::encode(data[msgOffset]);
                else if(_parameters.parameters()[i].size() == 2)
                    logRecord[i] = common::encode(data[msgOffset + 1],
                                                  data[msgOffset]);
                else if(_parameters.parameters()[i].size() == 3)
                    logRecord[i] = common::encode(data[msgOffset + 2],
                                                  data[msgOffset + 1],
                                                  data[msgOffset]);
                else if(_parameters.parameters()[i].size() == 4)
                    logRecord[i] = common::encode(data[msgOffset + 3],
                                                  data[msgOffset + 2],
                                                  data[msgOffset + 1],
                                                  data[msgOffset]);
            }
        }
    }
      if(!logRecord.empty())
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
  _channel1->stopPeriodicMsg(testerPresentMsgID);
}

void TCMDataLogger::pushRecord(TCMDataLogger::LogRecord &&record) {
  std::unique_lock<std::mutex> lock{_callbackMutex};
  _loggedRecords.emplace_back(std::move(record));
  _callbackCond.notify_all();
}

void TCMDataLogger::callbackFunction() {
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
    for (size_t i = 0; i < logRecord.values.size() && i < _parameters.parameters().size(); ++i) {
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
