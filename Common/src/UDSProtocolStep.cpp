#include "common/UDSProtocolStep.hpp"

namespace common {

	UDSProtocolStep::UDSProtocolStep(UDSStepType stepType, size_t maximumProgress, bool skipOnError)
		: _stepType{ stepType }
		, _currentProgress{}
		, _maximumProgress{ maximumProgress }
		, _skipOnError{ skipOnError }
	{
	}

	UDSProtocolStep::~UDSProtocolStep()
	{
	}

	bool UDSProtocolStep::process(bool previousFailed)
	{
		bool result = true;
		if (!(previousFailed && _skipOnError)) {
			try {
				result = processImpl();
			}
			catch (...) {
				result = false;
			}
		}
		setCurrentProgress(getMaximumProgress());
		return result;
	}

	size_t UDSProtocolStep::getCurrentProgress() const
	{
		std::unique_lock<std::mutex> lock(_mutex);
		return _currentProgress;
	}

	size_t UDSProtocolStep::getMaximumProgress() const
	{
		std::unique_lock<std::mutex> lock(_mutex);
		return _maximumProgress;
	}

	UDSStepType UDSProtocolStep::getStepType() const
	{
		return _stepType;
	}

	void UDSProtocolStep::setCurrentProgress(size_t currentProgress)
	{
		std::unique_lock<std::mutex> lock(_mutex);
		_currentProgress = currentProgress;
	}

	void UDSProtocolStep::setMaximumProgress(size_t maximumProgress)
	{
		std::unique_lock<std::mutex> lock(_mutex);
		_maximumProgress = maximumProgress;
	}

} // namespace common
