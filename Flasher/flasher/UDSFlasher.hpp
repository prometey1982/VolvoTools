#pragma once

#include "FlasherCallback.hpp"

#include <common/VBF.hpp>
#include <common/UDSProtocolBase.hpp>

#include <array>
#include <memory>
#include <mutex>

namespace j2534 {
	class J2534;
	class J2534Channel;
} // namespace j2534

namespace flasher {

	class UDSFlasher : public common::UDSProtocolBase {
	public:
		UDSFlasher(j2534::J2534& j2534, uint32_t cmId, const std::array<uint8_t, 5>& pin, const common::VBF& bootloader, const common::VBF& flash);
		~UDSFlasher();

#if 0
		void registerCallback(FlasherCallback& callback);
		void unregisterCallback(FlasherCallback& callback);
		void messageToCallbacks(const std::string& message);
		void stepToCallbacks(common::UDSStepType stepType);
#endif

	private:
		void setState(State newState);

	private:
		std::array<uint8_t, 5> _pin;
		common::VBF _bootloader;
		common::VBF _flash;
		std::vector<std::unique_ptr<j2534::J2534Channel>> _channels;
		std::vector<FlasherCallback*> _callbacks;
	};

} // namespace flasher
