#pragma once

#include <functional>
#include <mutex>
#include <map>
#include <vector>

namespace common {

class ChannelSubscriptions {
public:
    void notifyAll(const std::vector<uint8_t>& message);
    void remoteSubscription(int subscriptionId);
    int addSubscription(const std::function<bool(const std::vector<uint8_t>&)>& callback);
private:
    std::mutex _mutex;
    std::map<int, std::function<bool(const std::vector<uint8_t>&)>> _subscriptions;
};

}
