#pragma once

#include "CarPlatform.hpp"
#include "ConfigurationInfo.hpp"

#include <j2534/J2534Channel.hpp>

#include <memory>
#include <vector>

namespace common {

	struct CommonStepData {
		std::vector<std::unique_ptr<j2534::J2534Channel>> channels;
		std::vector<ConfigurationInfo> configurationInfo;
		CarPlatform carPlatform;
		uint32_t ecuId;
		uint32_t canId;
		size_t channelIndex;
	};

} // namespace common
