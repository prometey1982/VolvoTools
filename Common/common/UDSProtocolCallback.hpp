#pragma once

#include "UDSProtocolStep.hpp"

#include <chrono>
#include <string>

namespace common {

class UDSProtocolCallback {
public:
  UDSProtocolCallback() = default;
  virtual ~UDSProtocolCallback() = default;

  virtual void OnProgress(std::chrono::milliseconds timePoint,
                               size_t currentValue, size_t maxValue) = 0;
  virtual void OnMessage(const std::string &message) = 0;
  virtual void OnStep(UDSStepType stepType) {}
};

} // namespace common
