#pragma once

#include "UDSStepType.hpp"

#include <cinttypes>
#include <mutex>

namespace common {

	class UDSProtocolStep {
	public:
		UDSProtocolStep(UDSStepType stepType, size_t maximumProgress, bool skipOnError);
        virtual ~UDSProtocolStep();

		bool process(bool previousFailed);

		size_t getCurrentProgress() const;
		size_t getMaximumProgress() const;

		UDSStepType getStepType() const;

	protected:
		virtual bool processImpl() = 0;

		void setCurrentProgress(size_t currentProgress);
		void setMaximumProgress(size_t maximumProgress);

	private:
		UDSStepType _stepType;
		mutable std::mutex _mutex;

		size_t _currentProgress;
		size_t _maximumProgress;

		bool _skipOnError;
	};

} // namespace common
