#pragma once

#include "VBF.hpp"

#include <j2534/J2534.hpp>
#include <j2534/J2534Channel.hpp>

#include <memory>

namespace common {

	class D2ProtocolCommonSteps {
	public:
		static bool fallAsleep(const std::vector<std::unique_ptr<j2534::J2534Channel>>& channels);
        static bool startPBL(const j2534::J2534Channel& channel, uint8_t ecuId);
		static void wakeUp(const std::vector<std::unique_ptr<j2534::J2534Channel>>& channels);
        static bool transferData(const j2534::J2534Channel& channel, uint8_t ecuId, const VBF& data,
                                 const std::function<void(size_t)> progressCallback);
        static bool eraseFlash(const j2534::J2534Channel& channel, uint8_t ecuId, const VBF& data);
        static void jumpTo(const j2534::J2534Channel& channel, uint8_t ecuId, uint32_t addr);
		static bool startRoutine(const j2534::J2534Channel& channel, uint8_t ecuId, uint32_t addr);
	};

} // namespace common
