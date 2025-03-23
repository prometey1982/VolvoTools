#pragma once

#include "common/VBF.hpp"

#include "RequestProcessorBase.hpp"

#include <functional>

namespace common {

    class KWPProtocolCommonSteps {
	public:
		static bool authorize(const RequestProcessorBase& requestProcessor, const std::array<uint8_t, 5>& pin);
		static bool enterProgrammingSession(RequestProcessorBase& requestProcessor);
		static bool transferData(const RequestProcessorBase& requestProcessor, const VBF& data,
                                 const std::function<void(size_t)>& progressCallback);
		static bool eraseFlash(const RequestProcessorBase& requestProcessor, const VBF& data);
		static bool eraseFlash(const RequestProcessorBase& requestProcessor, const VBFChunk& data);
		static bool transferData(const RequestProcessorBase& requestProcessor, const VBFChunk& data,
			const std::function<void(size_t)>& progressCallback);

		static bool startRoutine(const RequestProcessorBase& requestProcessor, uint32_t addr);
	};

} // namespace common
