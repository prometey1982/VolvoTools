#pragma once

#include "CarPlatform.hpp"
#include "ConfigurationInfo.hpp"
#include "UDSCommonStepData.hpp"
#include "UDSProtocolStep.hpp"
#include "VBF.hpp"

#include <j2534/J2534.hpp>
#include <j2534/J2534Channel.hpp>


namespace common {

	class OpenChannelsStep : public UDSProtocolStep {
	public:
		OpenChannelsStep(j2534::J2534& j2534, CommonStepData& commonData);

		bool processImpl() override;

	private:
		j2534::J2534& _j2534;
		CommonStepData& _commonData;
	};

	class CloseChannelsStep : public UDSProtocolStep {
	public:
		CloseChannelsStep(CommonStepData& commonData);

		bool processImpl() override;

	private:
		CommonStepData& _commonData;
	};

	class FallingAsleepStep : public UDSProtocolStep {
	public:
		FallingAsleepStep(const CommonStepData& commonData);

		bool processImpl() override;

	private:
		const CommonStepData& _commonData;
	};

	class KeepAliveStep : public UDSProtocolStep {
	public:
		KeepAliveStep(const CommonStepData& commonData);

		bool processImpl() override;

	private:
		const CommonStepData& _commonData;
	};

	class WakeUpStep : public UDSProtocolStep {
	public:
		WakeUpStep(const CommonStepData& commonData);

		bool processImpl() override;

	private:
		const CommonStepData& _commonData;
	};

	class AuthorizingStep : public UDSProtocolStep {
	public:
		AuthorizingStep(const CommonStepData& commonData, const std::array<uint8_t, 5>& pin);

		bool processImpl() override;

	private:
		uint32_t generateKeyImpl(uint32_t hash, uint32_t input);
		uint32_t generateKey(const std::array<uint8_t, 5>& pin_array, const std::array<uint8_t, 3>& seed_array);

		const CommonStepData& _commonData;
		const std::array<uint8_t, 5>& _pin;
	};

	class DataTransferStep : public UDSProtocolStep {
	public:
		DataTransferStep(UDSStepType step, const CommonStepData& commonData, const VBF& data);

		bool processImpl() override;

	private:
		size_t getMaximumSize(const VBF& data);

		const CommonStepData& _commonData;
		const VBF& _data;
	};

	class FlashErasingStep : public UDSProtocolStep {
	public:
		FlashErasingStep(const CommonStepData& commonData, const VBF& flash);

		bool processImpl() override;

	private:
		const CommonStepData& _commonData;
		const VBF& _flash;
	};

	class StartRoutineStep : public UDSProtocolStep {
	public:
		StartRoutineStep(const CommonStepData& commonData, const VBFHeader& header);

		bool processImpl() override;

	private:
		const CommonStepData& _commonData;
		const VBFHeader& _header;
	};

} // namespace common
