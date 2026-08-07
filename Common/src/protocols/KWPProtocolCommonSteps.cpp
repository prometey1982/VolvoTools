#include "common/protocols/KWPProtocolCommonSteps.hpp"

#include "common/protocols/UDSRequest.hpp"
#include "common/protocols/UDSError.hpp"
#include "common/KeyGenerators.hpp"
#include "common/Util.hpp"

#define LOG_MODULE_NAME "common"
#include "common/LogHelper.hpp"

#include <array>
#include <thread>
#include <variant>

namespace common {

    bool KWPProtocolCommonSteps::authorize(const RequestProcessorBase& requestProcessor, const std::array<uint8_t, 5>& pin)
	{
        LOG_MODULE(TRACE) << "authorize enter";
        try {
			const auto seedResponse{ requestProcessor.process({ 0x27, 0x01 }) };
			if (seedResponse.size() < 6)
				return false;
            uint32_t key = generateKeyVAG(encodeBigEndian(seedResponse[5], seedResponse[4], seedResponse[3], seedResponse[2]));
			const auto keyResponse{ requestProcessor.process({ 0x27, 0x02 }, { (key >> 24) & 0xFF, (key >> 16) & 0xFF, (key >> 8) & 0xFF, key & 0xFF }) };
			return keyResponse.size() >= 3 && keyResponse[2] == 0x34;
		}
		catch (...) {
			return false;
		}
        LOG_MODULE(TRACE) << "authorize exit";
	}

	bool KWPProtocolCommonSteps::enterProgrammingSession(RequestProcessorBase& requestProcessor)
	{
        LOG_MODULE(TRACE) << "enterProgrammingSession enter";
		try {
			requestProcessor.process({ 0x10, 0x85 });
			std::this_thread::sleep_for(std::chrono::milliseconds(500));
			requestProcessor.disconnect();
			std::this_thread::sleep_for(std::chrono::milliseconds(500));
			if (requestProcessor.connect()) {
				return authorize(requestProcessor, {});
			}
			return false;
		}
		catch (...) {
			return false;
		}
        LOG_MODULE(TRACE) << "enterProgrammingSession exit";
	}

	bool KWPProtocolCommonSteps::transferData(const RequestProcessorBase& requestProcessor, const VBF& data,
                                              const std::function<void(size_t)>& progressCallback)
	{
        LOG_MODULE(TRACE) << "transferData enter (VBF)";
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
                const size_t maxSizeToTransfer = encodeBigEndian(downloadResponse[1], downloadResponse[0]) - 2;
				uint8_t chunkIndex = 1;
				for (size_t i = 0; i < chunk.data.size(); i += maxSizeToTransfer, ++chunkIndex) {
					const auto chunkEnd{ std::min(i + maxSizeToTransfer, chunk.data.size()) };
					std::vector<uint8_t> data{ 0x36, chunkIndex };
					data.insert(data.end(), chunk.data.cbegin() + i, chunk.data.cbegin() + chunkEnd);
					requestProcessor.process(std::move(data), {}, 60000);
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
        LOG_MODULE(TRACE) << "transferData exit (VBF)";
		return true;
	}

    bool KWPProtocolCommonSteps::eraseFlash(const RequestProcessorBase& requestProcessor, const VBF& data)
    {
        LOG_MODULE(TRACE) << "eraseFlash enter (VBF)";
		try {
			for (const auto& chunk : data.chunks) {
				const auto eraseAddr = toVector(chunk.writeOffset);
				const auto eraseEndAddr = toVector(chunk.writeOffset + static_cast<uint32_t>(chunk.data.size()) - 1);
				const auto eraseResult{ requestProcessor.process({ 0x31, 0xC4 }, {
					eraseAddr[1], eraseAddr[2], eraseAddr[3],
					eraseEndAddr[1], eraseEndAddr[2], eraseEndAddr[3], 0, 1, 2, 3, 4, 5 }, 10000) };
			}
			return true;
		}
		catch (...) {
			return false;
		}
        LOG_MODULE(TRACE) << "eraseFlash exit (VBF)";
	}

	size_t KWPProtocolCommonSteps::requestDownload(const RequestProcessorBase& requestProcessor, const VBFChunk& chunk)
    {
        LOG_MODULE(TRACE) << "requestDownload enter";
		try {
			const auto startAddr = chunk.writeOffset;
			const auto dataSize = chunk.data.size();
			const auto downloadResponse{ requestProcessor.process({ 0x34 },
				{ (startAddr >> 16) & 0xFF, (startAddr >> 8) & 0xFF, startAddr & 0xFF,
				0x11,
				(dataSize >> 16) & 0xFF, (dataSize >> 8) & 0xFF, dataSize & 0xFF }) };
            return encodeBigEndian(downloadResponse[1], downloadResponse[0]) - 2;;
		}
		catch (...) {
			return 0;
		}
        LOG_MODULE(TRACE) << "requestDownload exit";
	}

	bool KWPProtocolCommonSteps::eraseFlash(const RequestProcessorBase& requestProcessor, const VBFChunk& chunk)
    {
        LOG_MODULE(TRACE) << "eraseFlash enter (chunk)";
		try {
			const auto eraseAddr = toVector(chunk.writeOffset);
			const auto eraseEndAddr = toVector(chunk.writeOffset + static_cast<uint32_t>(chunk.data.size()) - 1);
			const auto eraseStartResult{ requestProcessor.process({ 0x31, 0xC4 }, {
				eraseAddr[1], eraseAddr[2], eraseAddr[3],
				eraseEndAddr[1], eraseEndAddr[2], eraseEndAddr[3], 0, 1, 2, 3, 4, 5 }, 10000) };
			const auto eraseResult{ requestProcessor.process({ 0x33, 0xC4 }, {}, 10000) };
			return true;
		}
		catch (...) {
			return false;
		}
        LOG_MODULE(TRACE) << "eraseFlash exit (chunk)";
	}

	bool KWPProtocolCommonSteps::transferData(const RequestProcessorBase& requestProcessor, const VBFChunk& chunk,
		size_t maxSizeToTransfer, const std::function<void(size_t)>& progressCallback)
	{
        LOG_MODULE(TRACE) << "transferData enter (chunk)";
        try {
			maxSizeToTransfer -= 5;
			for (size_t i = 0; i < chunk.data.size(); i += maxSizeToTransfer) {
				const auto chunkEnd{ std::min(i + maxSizeToTransfer, chunk.data.size()) };
				std::vector<uint8_t> dataToTransfer;
				dataToTransfer.insert(dataToTransfer.end(), chunk.data.cbegin() + i, chunk.data.cbegin() + chunkEnd);
				requestProcessor.process({ 0x36 }, std::move(dataToTransfer), 60000);
				progressCallback(chunkEnd - i);
			}
			const auto transferExitResponse{ requestProcessor.process({ 0x37 }) };
			// TODO: add calculation of chunks' CRC32 here and check for download results
				//&& transferExitResponse[2] == static_cast<uint8_t>(chunk.crc >> 8)
				//&& transferExitResponse[2] == static_cast<uint8_t>(chunk.crc);
		}
		catch (...) {
			return false;
		}
        LOG_MODULE(TRACE) << "transferData exit (chunk)";
		return true;
	}

	bool KWPProtocolCommonSteps::startRoutine(const RequestProcessorBase& requestProcessor, uint32_t addr)
    {
        LOG_MODULE(TRACE) << "startRoutine enter addr: " << std::hex << addr;
		const auto callAddr = common::toVector(addr);
		const auto callResult{ requestProcessor.process({ 0x31, 0x01, 0x03, 0x01, callAddr[0], callAddr[1], callAddr[2], callAddr[3] }) };
		if (callResult.size() < 6 || callResult[2] != 0x71 || callResult[3] != 0x01 || callResult[4] != 0x03
			|| callResult[5] != 0x01)
			return false;
        LOG_MODULE(TRACE) << "startRoutine exit";
		return true;
	}

} // namespace common
