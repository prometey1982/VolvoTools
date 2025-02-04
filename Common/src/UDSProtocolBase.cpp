#include "common/UDSProtocolBase.hpp"

#include "common/UDSProtocolCallback.hpp"
#include "common/UDSProtocolStep.hpp"

#include <j2534/J2534.hpp>
#include <j2534/J2534Channel.hpp>

#include <algorithm>
#include <optional>

namespace common {

	UDSProtocolBase::UDSProtocolBase(j2534::J2534& j2534, uint32_t canId)
		: _j2534{ j2534 }
		, _canId{ canId }
		, _currentState{ UDSProtocolBase::State::Initial }
	{
	}

	UDSProtocolBase::~UDSProtocolBase()
	{
	}

	void UDSProtocolBase::run()
	{
		setState(State::InProgress);
		bool failed = false;
		std::optional<UDSStepType> oldStepType;
		for (const auto& step : _steps) {
			if (oldStepType != step->getStepType()) {
				stepToCallbacks(step->getStepType());
				oldStepType = step->getStepType();
			}
			const bool currentStepFailed = !step->process(failed);
			failed |= currentStepFailed;
		}
		if (failed) {
			setState(State::Error);
		}
		else {
			setState(State::Done);
		}
	}

	void UDSProtocolBase::registerCallback(UDSProtocolCallback& callback)
	{
		std::unique_lock<std::mutex> lock(_mutex);
		_callbacks.push_back(&callback);
	}

	void UDSProtocolBase::unregisterCallback(UDSProtocolCallback& callback)
	{
		std::unique_lock<std::mutex> lock(_mutex);
		_callbacks.erase(std::remove(_callbacks.begin(), _callbacks.end(), &callback),
			_callbacks.end());
	}

	UDSProtocolBase::State UDSProtocolBase::getState() const
	{
		std::unique_lock<std::mutex> lock(_mutex);
		return _currentState;
	}

	size_t UDSProtocolBase::getCurrentProgress() const
	{
		size_t result = 0;
		for (const auto& step : _steps) {
			result += step->getCurrentProgress();
		}
		return result;
	}

	size_t UDSProtocolBase::getMaximumProgress() const
	{
		size_t result = 0;
		for (const auto& step : _steps) {
			result += step->getMaximumProgress();
		}
		return result;
	}

	j2534::J2534& UDSProtocolBase::getJ2534() const
	{
		return _j2534;
	}

	uint32_t UDSProtocolBase::getCanId() const
	{
		return _canId;
	}

	void UDSProtocolBase::registerStep(std::unique_ptr<UDSProtocolStep>&& step)
	{
		std::unique_lock<std::mutex> lock(_mutex);
		_steps.emplace_back(std::move(step));
	}

	void UDSProtocolBase::setState(State newState)
	{
		std::unique_lock<std::mutex> lock(_mutex);
		_currentState = newState;
	}

	std::mutex& UDSProtocolBase::getMutex() const
	{
		return _mutex;
	}

	void UDSProtocolBase::stepToCallbacks(UDSStepType step)
	{
		decltype(_callbacks) tmpCallbacks;
		{
			std::unique_lock<std::mutex> lock(_mutex);
			tmpCallbacks = _callbacks;
		}
		for (const auto& callback : tmpCallbacks) {
			callback->OnStep(step);
		}
	}

} // namespace common
