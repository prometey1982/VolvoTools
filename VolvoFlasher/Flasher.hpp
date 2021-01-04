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

  void flash(unsigned long baudrate, const std::vector<uint8_t> &bin);
  void stop();

  enum class State {
      Initial,
      InProgress,
      Done,
      Error
  };

  State getState() const;

private:
  void canGoToSleep(unsigned long protocolId, unsigned long flags);
  void canWakeUp(unsigned long protocolId, unsigned long flags);

  void writePreFlashMsgAndCheckAnswer(unsigned long protocolId,
                                      unsigned long flags);
  void writeBootloader(uint32_t writeOffset,
                       const std::vector<uint8_t> &bootloader,
                       unsigned long protocolId, unsigned long flags);
  void writeChunk(const std::vector<uint8_t> &bin, uint32_t beginOffset,
                  uint32_t endOffset, unsigned long protocolId,
                  unsigned long flags);
  void writeFlashMe7(const std::vector<uint8_t> &bin, unsigned long protocolId,
                     unsigned long flags);
  void writeFlashMe9(const std::vector<uint8_t> &bin, unsigned long protocolId,
                     unsigned long flags);

  void flasherFunction(const std::vector<uint8_t> bin, unsigned long protocolId,
                       unsigned long flags);

  void setState(State newState);

private:
  j2534::J2534 &_j2534;
  std::thread _flasherThread;
  mutable std::mutex _mutex;
  std::condition_variable _cond;

  State _currentState;

  std::vector<FlasherCallback *> _callbacks;

  std::unique_ptr<j2534::J2534Channel> _channel1;
  std::unique_ptr<j2534::J2534Channel> _channel2;
};

} // namespace flasher
