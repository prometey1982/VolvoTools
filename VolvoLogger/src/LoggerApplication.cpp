#include "LoggerApplication.hpp"

#include <j2534/J2534.hpp>
#include <logger/Logger.hpp>

#include <windows.h>

namespace logger {

/*static*/ LoggerApplication &LoggerApplication::instance() {
  static LoggerApplication s_app;
  return s_app;
}

void LoggerApplication::start(unsigned long baudrate,
                              j2534::J2534 &j2534,
                              const LogParameters &params,
                              const common::CarPlatform carPlatform,
                              uint32_t cmId,
                              const std::vector<LoggerCallback *> &callbacks) {
  _logger = std::make_unique<Logger>(j2534, carPlatform, cmId, std::string());
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
}

bool LoggerApplication::isStarted() const { return _logger != nullptr; }

LoggerApplication::LoggerApplication() {}

LoggerApplication::~LoggerApplication() { stop(); }

} // namespace logger
