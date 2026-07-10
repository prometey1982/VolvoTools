#pragma once

#include "common/VBF.hpp"

#include <functional>
#include <memory>

namespace common {
    class ICanChannel;

	class UDSProtocolCommonSteps {
	public:
		static bool fallAsleep(const std::vector<std::unique_ptr<ICanChannel>>& channels);
		static std::vector<unsigned long> keepAlive(ICanChannel& channel);
		static void wakeUp(const std::vector<std::unique_ptr<ICanChannel>>& channels);
		static bool authorize(ICanChannel& channel, uint32_t canId, const std::array<uint8_t, 5>& pin);
        static bool transferData(ICanChannel& channel, uint32_t canId, const VBF& data,
                                 const std::function<void(size_t)>& progressCallback);
        static bool transferChunk(ICanChannel& channel, uint32_t canId, const VBFChunk& chunk,
                                 const std::function<void(size_t)>& progressCallback);
        static bool eraseFlash(ICanChannel& channel, uint32_t canId, const VBF& data);
        static bool eraseChunk(ICanChannel& channel, uint32_t canId, const VBFChunk& chunk);
        static bool startRoutine(ICanChannel& channel, uint32_t canId, uint32_t addr);
        static bool checkValidApplication(ICanChannel& channel, uint32_t canId);
	};

} // namespace common
