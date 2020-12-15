#include "LoggerApplication.hpp"
#include "J2534.hpp"
#include "Logger.h"

#include <windows.h>

namespace logger {

/*static*/ LoggerApplication &LoggerApplication::instance() {
  static LoggerApplication s_app;
  return s_app;
}

void LoggerApplication::start(unsigned long baudrate,
                              std::unique_ptr<j2534::J2534> &&j2534,
                              const LogParameters &params,
                              const std::string &outputPath) {
  _j2534 = std::move(j2534);
  _logger = std::make_unique<Logger>(*_j2534);
  _logger->start(baudrate, params, outputPath);
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

bool LoggerApplication::isStarted() const
{
    return _logger != nullptr;
}

LoggerApplication::LoggerApplication() {
}

LoggerApplication::~LoggerApplication() { stop(); }

} // namespace logger
