#pragma once

#include "CEMCanMessage.hpp"

#include "../j2534/J2534_v0404.h"

#include <condition_variable>
#include <map>
#include <mutex>
#include <vector>
#include <thread>

namespace j2534 {
class J2534Channel;
} // namespace j2534

namespace common {

class ICanMessagesReceiver {
public:
    virtual ~ICanMessagesReceiver() {}
    /**
     * @brief Called then fully completed message was received over CAN
     * @param data CAN message
     * @return If this function returns false then receiving of CAN messages is stopped.
     *         If you need to continue receiving of CAN messages then you should return true.
     */
    virtual bool onCanMessage(const uint8_t* buffer, size_t bufferSize) = 0;
};

/**
 * @brief This class is used for sending and receiving CAN messages with preprocessing.
 */
class CanMessagesTransceiver {
public:
    explicit CanMessagesTransceiver(std::unique_ptr<j2534::J2534Channel> j2534Channel,
                                    unsigned long protocolID,
                                    unsigned long txFlags
                                    );
    ~CanMessagesTransceiver();

    void subscribe(ECUType ecuType, ICanMessagesReceiver& receiver);
    void unsubscribeAll(const ICanMessagesReceiver& receiver);

    void sendMessage(const std::vector<uint8_t>& data);
    void runRead(bool enabled);

private:
    void readThread();
    void processMessages(const std::vector<PASSTHRU_MSG>& msgs);

    std::unique_ptr<j2534::J2534Channel> _j2534Channel;
    unsigned long _protocolID;
    unsigned long _txFlags;
    std::mutex _mutex;
    std::condition_variable _cond;

    std::map<ECUType, std::vector<uint8_t>> _receivedMessages;
    std::multimap<ECUType, ICanMessagesReceiver*> _subscribers;

    bool _isReadEnabled;
    bool _isShutdown;
    std::thread _thread;
};

} // namespace common
