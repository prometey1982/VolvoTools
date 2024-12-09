#pragma once

namespace common {

class IChannel;

class MessageSubscription {
public:
    MessageSubscription(IChannel& channel, int subscriptionId);
    MessageSubscription(MessageSubscription&& rhs);
    ~MessageSubscription();

private:
    void swap(MessageSubscription& rhs);

private:
    IChannel* _channel;
    int _subscriptionId;
};

}
