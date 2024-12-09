#pragma once

#include <chrono>
#include <vector>

namespace logger {

class LoggerCallback {
public:
  LoggerCallback() = default;
  virtual ~LoggerCallback() = default;

  virtual void onLogMessage(std::chrono::milliseconds timePoint,
                            const std::vector<double> &values) = 0;
  virtual void onStatusChanged(bool started) = 0;
};

} // namespace logger
