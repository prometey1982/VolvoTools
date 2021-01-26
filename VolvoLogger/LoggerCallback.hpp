#pragma once

#include <vector>
#include <chrono>

namespace logger {

class LoggerCallback {
public:
  LoggerCallback() = default;
  virtual ~LoggerCallback() = default;

  virtual void onLogMessage(std::chrono::milliseconds timePoint,
                            const std::vector<double> &values) = 0;
  virtual void onStatusChanged(bool started) = 0;
};

}