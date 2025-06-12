#pragma once

#include "FlasherState.hpp"

#include <chrono>
#include <string>

namespace flasher {

class FlasherCallback {
public:
  FlasherCallback() = default;
  virtual ~FlasherCallback() = default;

  virtual void OnProgress(std::chrono::milliseconds timePoint,
                               size_t currentValue, size_t maxValue) = 0;
  virtual void OnState(FlasherState state) = 0;
};

} // namespace flasher
