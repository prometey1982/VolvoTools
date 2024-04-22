#include "LoggerApplication.hpp"
#include "../j2534/J2534.hpp"
#include "Logger.hpp"

#include <windows.h>

namespace logger {

/*static*/ LoggerApplication &LoggerApplication::instance() {
  static LoggerApplication s_app;
  return s_app;
}

void LoggerApplication::start(unsigned long baudrate,
                              const std::shared_ptr<j2534::J2534> &j2534,
                              const LogParameters &params,
                              const std::vector<LoggerCallback *> &callbacks) {
  _j2534 = j2534;
  _logger = std::make_unique<Logger>(*_j2534, common::CarPlatform::P2);
  for (const auto &callback : callbacks) {
    _logger->registerCallback(*callback);
  }
  _logger->start(baudrate, params);
}

void LoggerApplication::stop() {
  if (nullptr != _logger) {
    _logger->stop();
    _logger.reset();
  }
  if (nullptr != _j2534) {
    _j2534->PassThruClose();
    _j2534.reset();
  }
}

bool LoggerApplication::isStarted() const { return _logger != nullptr; }

LoggerApplication::LoggerApplication() {}

LoggerApplication::~LoggerApplication() { stop(); }

} // namespace logger
