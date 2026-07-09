#include "flasher/FlasherCallbackHolder.hpp"

namespace flasher {

void FlasherCallbackHolder::registerCallback(FlasherCallback &callback)
{
    std::unique_lock<std::mutex> lock{_mutex};
    _callbacks.push_back(&callback);
}

void FlasherCallbackHolder::unregisterCallback(FlasherCallback &callback)
{
    std::unique_lock<std::mutex> lock{_mutex};
    _callbacks.erase(std::remove(_callbacks.begin(), _callbacks.end(), &callback),
                     _callbacks.end());
}

std::vector<FlasherCallback *> FlasherCallbackHolder::getCallbacks() const
{
    std::unique_lock<std::mutex> lock(_mutex);
    return _callbacks;
}

} // namespace flasher
