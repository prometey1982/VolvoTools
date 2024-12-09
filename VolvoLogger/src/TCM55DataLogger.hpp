#pragma once

#include "LogParameters.hpp"

#include <chrono>
#include <condition_variable>
#include <deque>
#include <memory>
#include <mutex>
#include <thread>
#include <vector>

namespace j2534 {
class J2534;
class J2534Channel;
} // namespace j2534

namespace logger {
class LoggerCallback;

class TCM55DataLogger final {
public:
  explicit TCM55DataLogger(j2534::J2534 &j2534);
  ~TCM55DataLogger();

  void registerCallback(LoggerCallback &callback);
  void unregisterCallback(LoggerCallback &callback);

  void start(unsigned long baudrate, const LogParameters &parameters);
  void stop();

private:
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
  std::unique_ptr<j2534::J2534Channel> _channel3;
};

} // namespace logger
