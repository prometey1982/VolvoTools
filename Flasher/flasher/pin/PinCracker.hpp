#pragma once

#include "flasher/pin/PinCrackerSteps.hpp"
#include "flasher/pin/PinCrackerStorage.hpp"

#include <atomic>
#include <chrono>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <thread>
#include <vector>

namespace flasher {

class PinCracker {
public:
    enum class State {
        Initial,
        PreAuth,
        Work,
        PostAuth,
        Done,
        Error
    };

    enum class Direction {
        Up,
        Down
    };

    struct BusContext {
        std::unique_ptr<common::ICanChannel> channel;
        std::unique_ptr<PinCrackerSteps> steps;
    };

    PinCracker(std::vector<BusContext> buses,
               size_t ecuBusIndex,
               Direction direction,
               uint64_t startPin,
               std::function<void(State, uint64_t)> stateCallback,
               std::shared_ptr<PinCrackerStorage> storage = {});
    ~PinCracker();

    PinCracker(const PinCracker&) = delete;
    PinCracker& operator=(const PinCracker&) = delete;

    State getCurrentState() const;
    std::optional<uint64_t> getFoundPin() const;

    bool start();
    void stop();

private:
    void run();

    std::vector<BusContext> _buses;
    size_t _ecuBusIndex;
    Direction _direction;
    uint64_t _startPin;
    std::thread _thread;

    std::function<void(State, uint64_t)> _stateCallback;
    std::shared_ptr<PinCrackerStorage> _storage;

    mutable std::mutex _mutex;
    State _currentState{State::Initial};
    std::optional<uint64_t> _foundPin;
    std::atomic<bool> _stop{false};
};

} // namespace flasher
