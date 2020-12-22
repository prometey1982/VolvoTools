#pragma once

#include "../j2534/J2534.hpp"
#include "LogParameters.hpp"

#include <chrono>
#include <condition_variable>
#include <deque>
#include <memory>
#include <mutex>
#include <thread>
#include <vector>

namespace j2534 {
class J2534Channel;
}

namespace logger {

class LoggerCallback {
public:
  LoggerCallback() = default;
  virtual ~LoggerCallback() = default;

  virtual void OnLogMessage(std::chrono::milliseconds timePoint,
                            const std::vector<double> &values) = 0;
};

class Logger final {
public:
  explicit Logger(j2534::J2534 &j2534);
  ~Logger();

  void registerCallback(LoggerCallback &callback);
  void unregisterCallback(LoggerCallback &callback);

  void start(unsigned long baudrate, const LogParameters &parameters);
  void stop();

private:
  std::unique_ptr<j2534::J2534Channel> openChannel(unsigned long ProtocolID,
                                                   unsigned long Flags,
                                                   unsigned long Baudrate);
  std::unique_ptr<j2534::J2534Channel> openBridgeChannel();
  void startXonXoffMessageFiltering(j2534::J2534Channel &channel,
                                    unsigned long Flags);
  void registerParameters(unsigned long ProtocolID, unsigned long Flags);

  void logFunction(unsigned long protocolId, unsigned int flags);

  struct LogRecord {
    LogRecord() = default;
    LogRecord(std::chrono::milliseconds timePoint,
              std::vector<uint32_t> &&values)
        : timePoint{timePoint}, values(std::move(values)) {}
    std::chrono::milliseconds timePoint;
    std::vector<uint32_t> values;
  };

  void pushRecord(LogRecord &&record);
  void callbackFunction();

private:
  j2534::J2534 &_j2534;
  LogParameters _parameters;
  std::thread _loggingThread;
  std::thread _callbackThread;
  std::mutex _mutex;
  std::mutex _callbackMutex;
  std::condition_variable _cond;
  std::condition_variable _callbackCond;
  bool _stopped;

  std::deque<LogRecord> _loggedRecords;
  std::vector<LoggerCallback *> _callbacks;

  std::unique_ptr<j2534::J2534Channel> _channel1;
  //		std::unique_ptr<j2534::J2534Channel> _channel2;
  std::unique_ptr<j2534::J2534Channel> _channel3;
};

} // namespace logger
