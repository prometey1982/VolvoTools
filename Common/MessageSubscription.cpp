#include "MessageSubscription.hpp"

#include "IChannel.hpp"

#include <utility>

namespace common {

MessageSubscription::MessageSubscription(IChannel& channel, int subscriptionId)
    : _channel(&channel)
    , _subscriptionId(subscriptionId)
{
}

MessageSubscription::MessageSubscription(MessageSubscription&& rhs)
    : _channel{}
    , _subscriptionId{}
{
    swap(rhs);
}

MessageSubscription::~MessageSubscription()
{
//    if(_channel)
//        _channel->removeSubscription(_subscriptionId);
}

void MessageSubscription::swap(MessageSubscription& rhs)
{
    std::swap(_channel, rhs._channel);
    std::swap(_subscriptionId, rhs._subscriptionId);
}

}
