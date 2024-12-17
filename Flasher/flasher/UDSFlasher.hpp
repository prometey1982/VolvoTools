#pragma once

#include "FlasherCallback.hpp"
#include "FlasherStep.hpp"

#include <common/VBF.hpp>

#include <array>
#include <memory>
#include <mutex>

namespace j2534 {
class J2534;
class J2534Channel;
} // namespace j2534

namespace flasher {

class UDSStep;

class UDSFlasher {
public:
    UDSFlasher(j2534::J2534 &j2534, uint32_t cmId, const std::array<uint8_t, 5>& pin, const common::VBF& bootloader, const common::VBF& flash);
    ~UDSFlasher();

    void flash();

    enum class State { Initial, InProgress, Done, Error };

    State getState() const;
    size_t getCurrentProgress() const;
    size_t getMaximumProgress() const;

    void registerCallback(FlasherCallback& callback);
    void unregisterCallback(FlasherCallback& callback);
    void messageToCallbacks(const std::string& message);
    void stepToCallbacks(FlasherStep step);

private:
    void setState(State newState);

private:
    j2534::J2534& _j2534;
    uint32_t _cmId;
    std::array<uint8_t, 5> _pin;
    common::VBF _bootloader;
    common::VBF _flash;
    mutable std::mutex _mutex;
    State _currentState;
    std::vector<std::unique_ptr<j2534::J2534Channel>> _channels;
    std::vector<std::unique_ptr<flasher::UDSStep>> _steps;
    std::vector<FlasherCallback*> _callbacks;
    std::thread _flasherThread;
};

} // namespace flasher
