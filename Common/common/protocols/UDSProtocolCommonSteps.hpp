#pragma once

#include "common/VBF.hpp"

#include <j2534/J2534.hpp>
#include <j2534/J2534Channel.hpp>

#include <memory>

namespace common {

	class UDSProtocolCommonSteps {
	public:
		static std::vector<std::unique_ptr<j2534::J2534Channel>> openChannels(
			j2534::J2534& j2534, unsigned long baudrate, uint32_t canId);
		static bool fallAsleep(const std::vector<std::unique_ptr<j2534::J2534Channel>>& channels);
		static std::vector<unsigned long> keepAlive(const j2534::J2534Channel& channel);
		static void wakeUp(const std::vector<std::unique_ptr<j2534::J2534Channel>>& channels);
		static bool authorize(const j2534::J2534Channel& channel, uint32_t canId, const std::array<uint8_t, 5>& pin);
        static bool transferData(const j2534::J2534Channel& channel, uint32_t canId, const VBF& data,
                                 const std::function<void(size_t)>& progressCallback);
        static bool transferChunk(const j2534::J2534Channel& channel, uint32_t canId, const VBFChunk& chunk,
                                 const std::function<void(size_t)>& progressCallback);
        static bool eraseFlash(const j2534::J2534Channel& channel, uint32_t canId, const VBF& data);
        static bool eraseChunk(const j2534::J2534Channel& channel, uint32_t canId, const VBFChunk& chunk);
        static bool startRoutine(const j2534::J2534Channel& channel, uint32_t canId, uint32_t addr);
	};

} // namespace common
