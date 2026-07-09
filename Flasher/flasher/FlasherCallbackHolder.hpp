#pragma once

#include "FlasherCallback.hpp"

#include <mutex>
#include <vector>

namespace flasher {

class FlasherCallbackHolder {
public:
    void registerCallback(FlasherCallback& callback);
    void unregisterCallback(FlasherCallback& callback);

protected:
    std::vector<FlasherCallback*> getCallbacks() const;

private:
    mutable std::mutex _mutex;
    std::vector<FlasherCallback*> _callbacks;
};

} // namespace flasher
