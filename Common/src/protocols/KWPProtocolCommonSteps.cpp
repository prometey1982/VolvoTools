#include "common/protocols/KWPProtocolCommonSteps.hpp"

#include "common/protocols/UDSRequest.hpp"
#include "common/protocols/UDSError.hpp"
#include "common/Util.hpp"

#include <thread>
#include <variant>

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


    bool KWPProtocolCommonSteps::authorize(const RequestProcessorBase& requestProcessor, const std::array<uint8_t, 5>& pin)
	{
		try {
			const auto seedResponse{ requestProcessor.process({ 0x27, 0x01 }) };
			if (seedResponse.size() < 9)
				return false;
			std::array<uint8_t, 3> seed = { seedResponse[6], seedResponse[7], seedResponse[8] };
			uint32_t key = generateKey(pin, seed);
			const auto keyResponse{ requestProcessor.process({ 0x27, 0x02, (key >> 16) & 0xFF, (key >> 8) & 0xFF, key & 0xFF }) };
			return keyResponse.size() >= 6 && keyResponse[5] == 0x02;
		}
		catch (...) {
			return false;
		}
	}

    bool KWPProtocolCommonSteps::transferData(const RequestProcessorBase& requestProcessor, const VBF& data,
                                              const std::function<void(size_t)>& progressCallback)
	{
		try {
			for (const auto& chunk : data.chunks) {
				const auto startAddr = chunk.writeOffset;
				const auto dataSize = chunk.data.size();
				const auto downloadResponse{ requestProcessor.process({ 0x34, 0x00, 0x44,
					(startAddr >> 24) & 0xFF, (startAddr >> 16) & 0xFF, (startAddr >> 8) & 0xFF, startAddr & 0xFF,
					(dataSize >> 24) & 0xFF, (dataSize >> 16) & 0xFF, (dataSize >> 8) & 0xFF, dataSize & 0xFF }) };
				if (downloadResponse.size() < 2) {
					return false;
				}
				const size_t maxSizeToTransfer = encode(downloadResponse[1], downloadResponse[0]) - 2;
				uint8_t chunkIndex = 1;
				for (size_t i = 0; i < chunk.data.size(); i += maxSizeToTransfer, ++chunkIndex) {
					const auto chunkEnd{ std::min(i + maxSizeToTransfer, chunk.data.size()) };
					std::vector<uint8_t> data{ 0x36, chunkIndex };
					data.insert(data.end(), chunk.data.cbegin() + i, chunk.data.cbegin() + chunkEnd);
					requestProcessor.process(std::move(data), 60000);
                    progressCallback(chunkEnd - i);
				}
				const auto transferExitResponse{ requestProcessor.process({ 0x37 }) };
				return transferExitResponse.size() >= 4
					&& transferExitResponse[2] == static_cast<uint8_t>(chunk.crc >> 8)
					&& transferExitResponse[2] == static_cast<uint8_t>(chunk.crc);
			}
		}
		catch (...) {
			return false;
		}

		return true;
	}

    bool KWPProtocolCommonSteps::eraseFlash(const RequestProcessorBase& requestProcessor, const VBF& data)
	{
		for (const auto& chunk : data.chunks) {
			const auto eraseAddr = toVector(chunk.writeOffset);
			const auto eraseSize = toVector(static_cast<uint32_t>(chunk.data.size()));
			const auto eraseResult{ requestProcessor.process({ 0x31, 0x01, 0xff, 0x00,
				eraseAddr[0], eraseAddr[1], eraseAddr[2], eraseAddr[3],
				eraseSize[0], eraseSize[1], eraseSize[2], eraseSize[3] }) };
			if (eraseResult.size() < 8 || eraseResult[2] != 0x71 || eraseResult[3] != 0x01
				|| eraseResult[4] != 0xFF)
				return false;
		}
		return true;
	}

    bool KWPProtocolCommonSteps::startRoutine(const RequestProcessorBase& requestProcessor, uint32_t addr)
	{
		const auto callAddr = common::toVector(addr);
		const auto callResult{ requestProcessor.process({ 0x31, 0x01, 0x03, 0x01, callAddr[0], callAddr[1], callAddr[2], callAddr[3] }) };
		if (callResult.size() < 6 || callResult[2] != 0x71 || callResult[3] != 0x01 || callResult[4] != 0x03
			|| callResult[5] != 0x01)
			return false;
		return true;
	}

} // namespace common
