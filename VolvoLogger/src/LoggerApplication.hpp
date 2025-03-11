#pragma once

#include <common/CarPlatform.hpp>

#include <memory>
#include <string>
#include <vector>

namespace j2534 {
class J2534;
}

namespace logger {
class Logger;
class LogParameters;
class LoggerCallback;

class LoggerApplication final {
public:
  static LoggerApplication &instance();

  void start(unsigned long baudrate, j2534::J2534 &j2534,
             const LogParameters &params,
             common::CarPlatform carPlatform,
             uint32_t cmId,
             const std::vector<LoggerCallback *> &callbacks);
  void stop();

  bool isStarted() const;

private:
  LoggerApplication();
  ~LoggerApplication();

  std::unique_ptr<Logger> _logger;
};

} // namespace logger
