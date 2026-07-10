#pragma once

#include "common/VBF.hpp"

#include <functional>
#include <memory>

namespace common {
    class ICanChannel;

	class D2ProtocolCommonSteps {
	public:
		static bool fallAsleep(const std::vector<std::unique_ptr<ICanChannel>>& channels);
        static bool startPBL(ICanChannel& channel, uint8_t ecuId);
		static void wakeUp(const std::vector<std::unique_ptr<ICanChannel>>& channels);
        static bool transferData(ICanChannel& channel, uint8_t ecuId, const VBF& data,
                                 const std::function<void(size_t)>& progressCallback);
        static bool eraseFlash(ICanChannel& channel, uint8_t ecuId, const VBF& data);
        static void jumpTo(ICanChannel& channel, uint8_t ecuId, uint32_t addr);
        static bool startRoutine(ICanChannel& channel, uint8_t ecuId, uint32_t addr);
        static void setDIMTime(const std::vector<std::unique_ptr<ICanChannel>>& channels);
    };

} // namespace common
