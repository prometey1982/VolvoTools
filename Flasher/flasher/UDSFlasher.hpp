#pragma once

#include "FlasherStep.hpp"
#include "VBF.hpp"

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
    UDSFlasher(j2534::J2534 &j2534, uint32_t cmId, const std::vector<uint8_t>& pin, const VBF& bootloader, const VBF& flash);
    ~UDSFlasher();

    void flash();

    enum class State { Initial, InProgress, Done, Error };

    State getState() const;
    size_t getCurrentProgress() const;
    size_t getMaximumProgress() const;

private:
    void setState(State newState);

private:
    j2534::J2534& _j2534;
    uint32_t _cmId;
    std::vector<uint8_t> _pin;
    VBF _bootloader;
    VBF _flash;
    mutable std::mutex _mutex;
    State _currentState;
    std::vector<std::unique_ptr<j2534::J2534Channel>> _channels;
    std::vector<std::unique_ptr<flasher::UDSStep>> _steps;
};

} // namespace flasher
