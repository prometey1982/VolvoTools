#pragma once

#include "J2534.hpp"
#include "LogParameters.hpp"

#include <condition_variable>
#include <memory>
#include <mutex>
#include <thread>

namespace j2534 {
class J2534Channel;
}

namespace logger {

class Logger final {
public:
  explicit Logger(j2534::J2534 &j2534);
  ~Logger();

  void start(unsigned long baudrate, const LogParameters &parameters,
             const std::wstring &savePath);
  void stop();

private:
  std::unique_ptr<j2534::J2534Channel> openChannel(unsigned long ProtocolID,
                                                   unsigned long Flags,
                                                   unsigned long Baudrate);
  std::unique_ptr<j2534::J2534Channel> openBridgeChannel();
  void startXonXoffMessageFiltering(j2534::J2534Channel &channel,
                                    unsigned long Flags);
  void registerParameters(unsigned long ProtocolID, unsigned long Flags);

  void logFunction(unsigned long protocolId, unsigned int flags,
                   const std::wstring &savePath);

private:
  j2534::J2534 &_j2534;
  LogParameters _parameters;
  std::thread _loggingThread;
  std::mutex _mutex;
  std::condition_variable _cond;
  bool _stopped;

  std::unique_ptr<j2534::J2534Channel> _channel1;
  //		std::unique_ptr<j2534::J2534Channel> _channel2;
  std::unique_ptr<j2534::J2534Channel> _channel3;
};

} // namespace logger
