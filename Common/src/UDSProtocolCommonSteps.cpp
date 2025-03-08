#include "common/UDSProtocolCommonSteps.hpp"

#include "common/UDSError.hpp"
#include "common/UDSMessage.hpp"
#include "common/UDSRequest.hpp"
#include "common/Util.hpp"

namespace common {

	namespace {

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

	}

	std::vector<std::unique_ptr<j2534::J2534Channel>> UDSProtocolCommonSteps::openChannels(
		j2534::J2534& j2534, unsigned long baudrate, uint32_t canId)
	{
		std::vector<std::unique_ptr<j2534::J2534Channel>> result;
		result.emplace_back(openUDSChannel(j2534, baudrate, canId));
		result.emplace_back(openLowSpeedChannel(j2534, CAN_ID_BOTH));
		return result;
	}

	bool UDSProtocolCommonSteps::fallAsleep(std::vector<std::unique_ptr<j2534::J2534Channel>>& channels)
	{
		std::vector<std::vector<unsigned long>> msgIds(channels.size());
		for (size_t i = 0; i < channels.size(); ++i) {
			const auto ids = channels[i]->startPeriodicMsgs(UDSMessage(0x7DF, { 0x10, 0x02 }), 5);
			if (ids.empty()) {
				return false;
			}
			msgIds[i] = ids;
		}
		std::this_thread::sleep_for(std::chrono::seconds(2));
		for (size_t i = 0; i < channels.size(); ++i) {
			channels[i]->stopPeriodicMsg(msgIds[i]);
		}
		return true;
	}

	std::vector<unsigned long> UDSProtocolCommonSteps::keepAlive(j2534::J2534Channel& channel)
	{
		return channel.startPeriodicMsgs(UDSMessage(0x7DF, { 0x3E, 0x80 }), 1900);
	}

	void UDSProtocolCommonSteps::wakeUp(std::vector<std::unique_ptr<j2534::J2534Channel>>& channels)
	{
		(void)channels;
	}

	bool UDSProtocolCommonSteps::authorize(j2534::J2534Channel& channel, uint32_t canId,
		const std::array<uint8_t, 5>& pin)
	{
		try {
			channel.clearRx();
			UDSMessage requestSeedMsg(canId, { 0x27, 0x01 });
			UDSRequest seedRequest(canId, { 0x27, 0x01 });
			const auto seedResponse(seedRequest.process(channel));
			if (seedResponse.size() < 9)
				return false;
			std::array<uint8_t, 3> seed = { seedResponse[6], seedResponse[7], seedResponse[8] };
			uint32_t key = generateKey(pin, seed);
			channel.clearRx();
			UDSRequest keyRequest(canId, { 0x27, 0x02, (key >> 16) & 0xFF, (key >> 8) & 0xFF, key & 0xFF });
			const auto keyResponse(keyRequest.process(channel));
			return keyResponse.size() >= 6 && keyResponse[5] == 0x02;
		}
		catch (...) {
			return false;
		}
	}

	bool UDSProtocolCommonSteps::transferData(j2534::J2534Channel& channel, uint32_t canId, const VBF& data)
	{
		for (const auto& chunk : data.chunks) {
			const auto startAddr = chunk.writeOffset;
			const auto dataSize = chunk.data.size();
			UDSMessage requestDownloadMsg(canId, { 0x34, 0x00, 0x44,
				(startAddr >> 24) & 0xFF, (startAddr >> 16) & 0xFF, (startAddr >> 8) & 0xFF, startAddr & 0xFF,
				(dataSize >> 24) & 0xFF, (dataSize >> 16) & 0xFF, (dataSize >> 8) & 0xFF, dataSize & 0xFF });
			unsigned long numMsgs;
			channel.clearRx();
			if (channel.writeMsgs(requestDownloadMsg, numMsgs) != STATUS_NOERROR || numMsgs < 1) {
				return false;
			}
			const auto downloadResponse = readMessageCheckAndGet(channel, { 0x74 }, { 0x20 }, 10);
			if (downloadResponse.empty()) {
				return false;
			}
			const size_t maxSizeToTransfer = encode(downloadResponse[1], downloadResponse[0]) - 2;
			uint8_t chunkIndex = 1;
			for (size_t i = 0; i < chunk.data.size(); i += maxSizeToTransfer, ++chunkIndex) {
				const auto chunkEnd = std::min(i + maxSizeToTransfer, chunk.data.size());
				std::vector<uint8_t> data{ 0x36, chunkIndex };
				data.insert(data.end(), chunk.data.cbegin() + i, chunk.data.cbegin() + chunkEnd);
				UDSMessage transferMsg(canId, std::move(data));
				channel.clearRx();
				if (channel.writeMsgs(transferMsg, numMsgs, 60000) != STATUS_NOERROR || numMsgs < 1) {
					return false;
				}
				if (!readMessageAndCheck(channel, { 0x76 }, { chunkIndex }, 10)) {
					return false;
				}
			}
			UDSMessage transferExitMsg(canId, { 0x37 });
			channel.clearRx();
			if (channel.writeMsgs(transferExitMsg, numMsgs) != STATUS_NOERROR || numMsgs < 1) {
				return false;
			}
			if (!readMessageAndCheck(channel, { 0x77 }, { static_cast<uint8_t>(chunk.crc >> 8), static_cast<uint8_t>(chunk.crc) }, 10)) {
				return false;
			}
		}

		return true;
	}

	bool UDSProtocolCommonSteps::eraseFlash(j2534::J2534Channel& channel, uint32_t canId, const VBF& data)
	{
		for (const auto& chunk : data.chunks) {
			const auto eraseAddr = toVector(chunk.writeOffset);
			const auto eraseSize = toVector(static_cast<uint32_t>(chunk.data.size()));
			UDSMessage eraseRoutineMsg(canId, { 0x31, 0x01, 0xff, 0x00,
				eraseAddr[0], eraseAddr[1], eraseAddr[2], eraseAddr[3],
				eraseSize[0], eraseSize[1], eraseSize[2], eraseSize[3] });
			unsigned long numMsgs;
			if (channel.writeMsgs(eraseRoutineMsg, numMsgs) != STATUS_NOERROR || numMsgs < 1) {
				return false;
			}
			if (!readMessageAndCheck(channel, { 0x71, 0x01, 0xff, 0x00, 0x00, 0x00 }, {}, 10)) {
				return false;
			}
		}
		return true;
	}

	bool UDSProtocolCommonSteps::startRoutine(j2534::J2534Channel& channel, uint32_t canId, uint32_t addr)
	{
		const auto callAddr = common::toVector(addr);
		common::UDSMessage startRoutineMsg(canId, { 0x31, 0x01, 0x03, 0x01, callAddr[0], callAddr[1], callAddr[2], callAddr[3] });
		unsigned long numMsgs;
		if (channel.writeMsgs(startRoutineMsg, numMsgs) != STATUS_NOERROR || numMsgs < 1) {
			return false;
		}
		if (!common::readMessageAndCheck(channel, { 0x71, 0x01, 0x03, 0x01 }, {}, 10)) {
			return false;
		}
		return true;
	}

} // namespace common
