#pragma once

#include <condition_variable>
#include <mutex>
#include <thread>
#include <vector>

namespace j2534 {
class J2534;
class J2534Channel;
} // namespace j2534

namespace flasher {

class FlasherCallback {
public:
  FlasherCallback() = default;
  virtual ~FlasherCallback() = default;

  virtual void OnFlashProgress(std::chrono::milliseconds timePoint,
                               size_t currentValue, size_t maxValue) = 0;
};

class Flasher final {
public:
  explicit Flasher(j2534::J2534 &j2534);
  ~Flasher();

  void registerCallback(FlasherCallback &callback);
  void unregisterCallback(FlasherCallback &callback);

  void flash(unsigned long baudrate, const std::vector<uint8_t>& bin);
  void stop();

private:
    void flasherFunction(const std::vector<uint8_t> bin);

private:
  j2534::J2534 &_j2534;
  std::thread _flasherThread;
  std::mutex _mutex;
  std::condition_variable _cond;
  bool _stopped;

  std::vector<FlasherCallback *> _callbacks;

  std::unique_ptr<j2534::J2534Channel> _channel1;
  std::unique_ptr<j2534::J2534Channel> _channel2;
};

} // namespace flasher
