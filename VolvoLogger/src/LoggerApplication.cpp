#include "LoggerApplication.hpp"

#include <j2534/J2534.hpp>
#include <logger/Logger.hpp>

#include <windows.h>

#include <easylogging++.h>

#include <cstdint>

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
  LOG(DEBUG) << "LoggerApplication creating logger";
  _logger = std::make_unique<Logger>(j2534, carPlatform, cmId, std::string());
  LOG(DEBUG) << "LoggerApplication registering callbacks count=" << callbacks.size();
  for (size_t i = 0; i < callbacks.size(); ++i) {
    const auto callback = callbacks[i];
    LOG(DEBUG) << "LoggerApplication registering callback[" << i << "]=0x"
      << std::hex << reinterpret_cast<uintptr_t>(callback);
    if (callback == nullptr) {
      LOG(ERROR) << "LoggerApplication received null callback at index " << std::dec << i;
      continue;
    }
    _logger->registerCallback(*callback);
  }
  LOG(DEBUG) << "LoggerApplication starting logger";
  _logger->start(baudrate, params);
  LOG(DEBUG) << "LoggerApplication start returned";
}

void LoggerApplication::stop() {
  if (nullptr != _logger) {
    _logger->stop();
    _logger.reset();
  }
}

bool LoggerApplication::isStarted() const { return _logger != nullptr && _logger->isStarted(); }

LoggerApplication::LoggerApplication() {}

LoggerApplication::~LoggerApplication() { stop(); }

} // namespace logger
