#include "flasher/pin/PinCracker.hpp"

#include "common/ICanChannel.hpp"
#include "common/Util.hpp"

#include <chrono>
#include <thread>

namespace flasher {

PinCracker::PinCracker(std::vector<BusContext> buses,
                       size_t ecuBusIndex,
                       Direction direction,
                       uint64_t startPin,
                       std::function<void(State, uint64_t)> stateCallback,
                       std::shared_ptr<PinCrackerStorage> storage)
    : _buses{ std::move(buses) }
    , _ecuBusIndex{ ecuBusIndex }
    , _direction{ direction }
    , _startPin{ startPin }
    , _stateCallback{ std::move(stateCallback) }
    , _storage{ storage ? storage : std::make_shared<NullPinCrackerStorage>() }
{
}

PinCracker::~PinCracker()
{
    if (_thread.joinable()) {
        _thread.join();
    }
}

PinCracker::State PinCracker::getCurrentState() const
{
    std::unique_lock<std::mutex> lock{ _mutex };
    return _currentState;
}

std::optional<uint64_t> PinCracker::getFoundPin() const
{
    std::unique_lock<std::mutex> lock{ _mutex };
    return _foundPin;
}

bool PinCracker::start()
{
    _thread = std::thread([this] { run(); });
    return true;
}

void PinCracker::stop()
{
    _stop = true;
}

void PinCracker::run()
{
    auto setState = [this](State newState, uint64_t pin = 0) {
        {
            std::unique_lock<std::mutex> lock{ _mutex };
            _currentState = newState;
        }
        if (_stateCallback) {
            _stateCallback(newState, pin);
        }
    };

    // preAuth на всех шинах (каждая шина — своим протоколом)
    setState(State::PreAuth);
    for (auto& bus : _buses) {
        if (!bus.steps->preAuth(*bus.channel)) {
            setState(State::Error);
            return;
        }
    }

    // keepAlive только на шине целевого ЭБУ
    auto& ecuBus = _buses[_ecuBusIndex];
    auto keepAliveIds = ecuBus.steps->startKeepAlive(*ecuBus.channel);

    // Цикл перебора ПИНов
    auto endPin = (_direction == Direction::Up)
        ? ecuBus.steps->getMaxPin()
        : ecuBus.steps->getMinPin();
    auto step = (_direction == Direction::Up) ? 1 : -1;
    auto pin = _startPin;
    auto retryDelay = ecuBus.steps->getRetryDelay();

    while ((_direction == Direction::Up ? pin <= endPin : pin >= endPin) && !_stop) {
        // Пропустить уже проверенные
        while (!_stop && _storage->isChecked(pin)) {
            pin += step;
        }
        if (_stop) break;

        setState(State::Work, pin);

        if (ecuBus.steps->tryPin(*ecuBus.channel, pin)) {
            _foundPin = pin;
            _storage->markChecked(pin);
            break;
        }

        _storage->markChecked(pin);
        pin += step;

        if (retryDelay.count() > 0 && !_stop) {
            std::this_thread::sleep_for(retryDelay);
        }
    }

    ecuBus.steps->stopKeepAlive(keepAliveIds);

    // postAuth на всех шинах
    setState(State::PostAuth);
    for (auto& bus : _buses) {
        bus.steps->postAuth(*bus.channel);
    }

    _storage->flush();

    setState(_foundPin ? State::Done : State::Error, _foundPin.value_or(0));
}

} // namespace flasher
