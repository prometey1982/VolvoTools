#include "flasher/UDSFlasher.hpp"

#include <j2534/J2534.hpp>
#include <j2534/J2534Channel.hpp>

#include <common/UDSMessage.hpp>
#include <common/Util.hpp>

#include <optional>
#include <unordered_map>

namespace flasher {

	namespace {

		j2534::J2534Channel& getChannel(uint32_t cmId, const std::vector<std::unique_ptr<j2534::J2534Channel>>& channels)
		{
			static const std::unordered_map<uint32_t, size_t> CMMap = {
				{0x7E0, 0}
			};
			return *channels[CMMap.at(cmId)];
		}
	}

	class OpenChannelsStep : public common::UDSProtocolStep {
	public:
		OpenChannelsStep(j2534::J2534& j2534, uint32_t cmId, std::vector<std::unique_ptr<j2534::J2534Channel>>& channels)
			: UDSProtocolStep{ common::UDSStepType::OpenChannels, 100, true }
			, _j2534{ j2534 }
			, _cmId{ cmId }
			, _channels{ channels }
		{
		}

		bool processImpl() override
		{
			_channels.emplace_back(common::openUDSChannel(_j2534, 500000, _cmId));
			_channels.emplace_back(common::openLowSpeedChannel(_j2534, CAN_ID_BOTH));
			return true;
		}

	private:
		j2534::J2534& _j2534;
		uint32_t _cmId;
		std::vector<std::unique_ptr<j2534::J2534Channel>>& _channels;
	};

	class CloseChannelsStep : public common::UDSProtocolStep {
	public:
		CloseChannelsStep(std::vector<std::unique_ptr<j2534::J2534Channel>>& channels)
			: UDSProtocolStep{ common::UDSStepType::CloseChannels, 100, false }
			, _channels{ channels }
		{
		}

		bool processImpl() override
		{
			_channels.clear();
			return true;
		}

	private:
		std::vector<std::unique_ptr<j2534::J2534Channel>>& _channels;
	};

	class FallingAsleepStep : public common::UDSProtocolStep {
	public:
		FallingAsleepStep(const std::vector<std::unique_ptr<j2534::J2534Channel>>& channels)
			: UDSProtocolStep{ common::UDSStepType::FallingAsleep, 100, true }
			, _channels{ channels }
		{
		}

		bool processImpl() override
		{
			std::vector<std::vector<unsigned long>> msgIds(_channels.size());
			for (size_t i = 0; i < _channels.size(); ++i) {
				const auto ids = _channels[i]->startPeriodicMsgs(common::UDSMessage(0x7DF, { 0x10, 0x02 }), 5);
				if (ids.empty()) {
					return false;
				}
				msgIds[i] = ids;
			}
			std::this_thread::sleep_for(std::chrono::seconds(2));
			for (size_t i = 0; i < _channels.size(); ++i) {
				_channels[i]->stopPeriodicMsg(msgIds[i]);
			}
			return true;
		}

	private:
		const std::vector<std::unique_ptr<j2534::J2534Channel>>& _channels;
	};

	class KeepAliveStep : public common::UDSProtocolStep {
	public:
		KeepAliveStep(const std::vector<std::unique_ptr<j2534::J2534Channel>>& channels, uint32_t cmId)
			: UDSProtocolStep{ common::UDSStepType::FallingAsleep, 100, true }
			, _channels{ channels }
			, _cmId{ cmId }
		{
		}

		bool processImpl() override
		{
			const auto& channel = getChannel(_cmId, _channels);
			if (channel.startPeriodicMsgs(common::UDSMessage(0x7DF, { 0x3E, 0x80 }), 1900).empty())
			{
				return false;
			}
			return true;
		}

	private:
		const std::vector<std::unique_ptr<j2534::J2534Channel>>& _channels;
		uint32_t _cmId;
	};

	class WakeUpStep : public common::UDSProtocolStep {
	public:
		WakeUpStep(const std::vector<std::unique_ptr<j2534::J2534Channel>>& channels)
			: UDSProtocolStep{ common::UDSStepType::WakeUp, 100, false }
			, _channels{ channels }
		{
		}

		bool processImpl() override
		{
			return true;
		}

	private:
		const std::vector<std::unique_ptr<j2534::J2534Channel>>& _channels;
	};

	class AuthorizingStep : public common::UDSProtocolStep {
	public:
		AuthorizingStep(const std::vector<std::unique_ptr<j2534::J2534Channel>>& channels,
			uint32_t cmId, const std::array<uint8_t, 5>& pin)
			: UDSProtocolStep{ common::UDSStepType::Authorizing, 100, true }
			, _channels{ channels }
			, _cmId{ cmId }
			, _pin{ pin }
		{
		}

		bool processImpl() override
		{
			const auto& channel = getChannel(_cmId, _channels);
			channel.clearRx();
			common::UDSMessage requestSeedMsg(_cmId, { 0x27, 0x01 });
			unsigned long numMsgs;
			if (channel.writeMsgs(requestSeedMsg, numMsgs) != STATUS_NOERROR || numMsgs < 1) {
				return false;
			}
			const auto seedResponse = common::readMessageCheckAndGet(channel, { 0x67 }, { 0x01 });
			std::array<uint8_t, 3> seed = { seedResponse[0], seedResponse[1], seedResponse[2] };
			uint32_t key = generateKey(_pin, seed);
			channel.clearRx();
			common::UDSMessage sendKeyMsg(_cmId, { 0x27, 0x02, (key >> 16) & 0xFF, (key >> 8) & 0xFF, key & 0xFF });
			if (channel.writeMsgs(sendKeyMsg, numMsgs) != STATUS_NOERROR || numMsgs < 1) {
				return false;
			}
			return common::readMessageAndCheck(channel, { 0x67 }, { 0x02 }, 10);
		}

	private:
		uint32_t generateKeyImpl(uint32_t hash, uint32_t input)
		{
			for (size_t i = 0; i < 32; ++i)
			{
				const bool is_bit_set = (hash ^ input) & 1;
				input >>= 1;
				hash >>= 1;
				if (is_bit_set)
					hash = (hash | 0x800000) ^ 0x109028;
			}
			return hash;
		}

		uint32_t generateKey(const std::array<uint8_t, 5>& pin_array, const std::array<uint8_t, 3>& seed_array)
		{
			const uint32_t high_part = pin_array[4] << 24 | pin_array[3] << 16 | pin_array[2] << 8 | pin_array[1];
			const uint32_t low_part = pin_array[0] << 24 | seed_array[2] << 16 | seed_array[1] << 8 | seed_array[0];
			unsigned int hash = 0xC541A9;
			hash = generateKeyImpl(hash, low_part);
			hash = generateKeyImpl(hash, high_part);
			uint32_t result = ((hash & 0xF00000) >> 12) | hash & 0xF000 | (uint8_t)(16 * hash)
				| ((hash & 0xFF0) << 12) | ((hash & 0xF0000) >> 16);
			return result;
		}

		const std::vector<std::unique_ptr<j2534::J2534Channel>>& _channels;
		uint32_t _cmId;
		const std::array<uint8_t, 5>& _pin;
	};

	class DataTransferStep : public common::UDSProtocolStep {
		static size_t getMaximumSize(const common::VBF& data)
		{
			size_t result = 0;
			for (const auto chunk : data.chunks) {
				result += chunk.data.size();
			}
			return result;
		}
	public:
		DataTransferStep(common::UDSStepType step, const std::vector<std::unique_ptr<j2534::J2534Channel>>& channels, uint32_t cmId,
			const common::VBF& data)
			: UDSProtocolStep{ step, getMaximumSize(data), true }
			, _channels{ channels }
			, _cmId{ cmId }
			, _data{ data }
		{
		}

		bool processImpl() override
		{
			const auto& channel = getChannel(_cmId, _channels);
			for (const auto& chunk : _data.chunks) {
				const auto startAddr = chunk.writeOffset;
				const auto dataSize = chunk.data.size();
				common::UDSMessage requestDownloadMsg(_cmId, { 0x34, 0x00, 0x44,
					(startAddr >> 24) & 0xFF, (startAddr >> 16) & 0xFF, (startAddr >> 8) & 0xFF, startAddr & 0xFF,
					(dataSize >> 24) & 0xFF, (dataSize >> 16) & 0xFF, (dataSize >> 8) & 0xFF, dataSize & 0xFF });
				unsigned long numMsgs;
				channel.clearRx();
				if (channel.writeMsgs(requestDownloadMsg, numMsgs) != STATUS_NOERROR || numMsgs < 1) {
					return false;
				}
				const auto downloadResponse = common::readMessageCheckAndGet(channel, { 0x74 }, { 0x20 }, 10);
				if (downloadResponse.empty()) {
					return false;
				}
				const size_t maxSizeToTransfer = common::encode(downloadResponse[1], downloadResponse[0]) - 2;
				uint8_t chunkIndex = 1;
				for (size_t i = 0; i < chunk.data.size(); i += maxSizeToTransfer, ++chunkIndex) {
					const auto chunkEnd = std::min(i + maxSizeToTransfer, chunk.data.size());
					std::vector<uint8_t> data{ 0x36, chunkIndex };
					data.insert(data.end(), chunk.data.cbegin() + i, chunk.data.cbegin() + chunkEnd);
					common::UDSMessage transferMsg(_cmId, std::move(data));
					channel.clearRx();
					if (channel.writeMsgs(transferMsg, numMsgs, 60000) != STATUS_NOERROR || numMsgs < 1) {
						return false;
					}
					if (!common::readMessageAndCheck(channel, { 0x76 }, { chunkIndex }, 10)) {
						return false;
					}
				}
				common::UDSMessage transferExitMsg(_cmId, { 0x37 });
				channel.clearRx();
				if (channel.writeMsgs(transferExitMsg, numMsgs) != STATUS_NOERROR || numMsgs < 1) {
					return false;
				}
				if (!common::readMessageAndCheck(channel, { 0x77 }, { static_cast<uint8_t>(chunk.crc >> 8), static_cast<uint8_t>(chunk.crc) }, 10)) {
					return false;
				}
			}

			return true;
		}

	private:
		const std::vector<std::unique_ptr<j2534::J2534Channel>>& _channels;
		uint32_t _cmId;
		const common::VBF& _data;
	};

	class FlashErasingStep : public common::UDSProtocolStep {
	public:
		FlashErasingStep(const std::vector<std::unique_ptr<j2534::J2534Channel>>& channels, uint32_t cmId, const common::VBF& flash)
			: UDSProtocolStep{ common::UDSStepType::FlashErasing, 100 * flash.chunks.size(), true }
			, _channels{ channels }
			, _cmId{ cmId }
			, _flash{ flash }
		{
		}

		bool processImpl() override
		{
			const auto& channel = getChannel(_cmId, _channels);
			for (const auto& chunk : _flash.chunks) {
				const auto eraseAddr = common::toVector(chunk.writeOffset);
				const auto eraseSize = common::toVector(chunk.data.size());
				common::UDSMessage eraseRoutineMsg(_cmId, { 0x31, 0x01, 0xff, 0x00,
					eraseAddr[0], eraseAddr[1], eraseAddr[2], eraseAddr[3],
					eraseSize[0], eraseSize[1], eraseSize[2], eraseSize[3] });
				unsigned long numMsgs;
				if (channel.writeMsgs(eraseRoutineMsg, numMsgs) != STATUS_NOERROR || numMsgs < 1) {
					return false;
				}
				if (!common::readMessageAndCheck(channel, { 0x71, 0x01, 0xff, 0x00, 0x00, 0x00 }, {}, 10)) {
					return false;
				}
			}
			return true;
		}

	private:
		const std::vector<std::unique_ptr<j2534::J2534Channel>>& _channels;
		uint32_t _cmId;
		const common::VBF& _flash;
	};

	class StartRoutineStep : public common::UDSProtocolStep {
	public:
		StartRoutineStep(const std::vector<std::unique_ptr<j2534::J2534Channel>>& channels, uint32_t cmId, const common::VBFHeader& header)
			: UDSProtocolStep{ common::UDSStepType::BootloaderStarting, 100, true }
			, _channels{ channels }
			, _cmId{ cmId }
			, _header{ header }
		{
		}

		bool processImpl() override
		{
			const auto& channel = getChannel(_cmId, _channels);
			const auto callAddr = common::toVector(_header.call);
			common::UDSMessage startRoutineMsg(_cmId, { 0x31, 0x01, 0x03, 0x01, callAddr[0], callAddr[1], callAddr[2], callAddr[3] });
			unsigned long numMsgs;
			if (channel.writeMsgs(startRoutineMsg, numMsgs) != STATUS_NOERROR || numMsgs < 1) {
				return false;
			}
			if (!common::readMessageAndCheck(channel, { 0x71, 0x01, 0x03, 0x01 }, {}, 10)) {
				return false;
			}
			return true;
		}

	private:
		const std::vector<std::unique_ptr<j2534::J2534Channel>>& _channels;
		uint32_t _cmId;
		const common::VBFHeader& _header;
	};

	UDSFlasher::UDSFlasher(j2534::J2534& j2534, uint32_t cmId, const std::array<uint8_t, 5>& pin,
		const common::VBF& bootloader, const common::VBF& flash)
		: UDSProtocolBase{ j2534, cmId }
		, _pin{ pin }
		, _bootloader{ bootloader }
		, _flash{ flash }
	{
		registerStep(std::make_unique<OpenChannelsStep>(getJ2534(), getCmId(), _channels));
		registerStep(std::make_unique<FallingAsleepStep>(_channels));
		registerStep(std::make_unique<KeepAliveStep>(_channels, getCmId()));
		registerStep(std::make_unique<AuthorizingStep>(_channels, getCmId(), _pin));
		registerStep(std::make_unique<DataTransferStep>(common::UDSStepType::BootloaderLoading, _channels, getCmId(), _bootloader));
		registerStep(std::make_unique<StartRoutineStep>(_channels, getCmId(), _bootloader.header));
		registerStep(std::make_unique<FlashErasingStep>(_channels, getCmId(), _flash));
		registerStep(std::make_unique<DataTransferStep>(common::UDSStepType::FlashLoading, _channels, getCmId(), _flash));
		registerStep(std::make_unique<WakeUpStep>(_channels));
		registerStep(std::make_unique<CloseChannelsStep>(_channels));
	}

	UDSFlasher::~UDSFlasher()
	{
	}
#if 0
	void UDSFlasher::registerCallback(FlasherCallback& callback)
	{
		std::unique_lock<std::mutex> lock{ getMutex() };
		_callbacks.push_back(&callback);
	}

	void UDSFlasher::unregisterCallback(FlasherCallback& callback)
	{
		std::unique_lock<std::mutex> lock{ getMutex() };
		_callbacks.erase(std::remove(_callbacks.begin(), _callbacks.end(), &callback),
			_callbacks.end());
	}

	void UDSFlasher::messageToCallbacks(const std::string& message) {
		decltype(_callbacks) tmpCallbacks;
		{
			std::unique_lock<std::mutex> lock(getMutex());
			tmpCallbacks = _callbacks;
		}
		for (const auto& callback : tmpCallbacks) {
			callback->OnMessage(message);
		}
	}
#endif

} // namespace flasher
