#include "common/protocols/UDSProtocolCommonSteps.hpp"

#include "common/protocols/UDSRequest.hpp"
#include "common/protocols/UDSError.hpp"
#include "common/Util.hpp"

#include <easylogging++.h>

#include <algorithm>
#include <chrono>
#include <set>
#include <thread>
#include <utility>

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

        std::string pinToHexString(const std::array<uint8_t, 5>& pin)
        {
            return toHexString({pin[0], pin[1], pin[2], pin[3], pin[4]});
        }

        bool rangesOverlap(uint32_t firstAddr, uint32_t firstSize, uint32_t secondAddr, uint32_t secondSize)
        {
            if (firstSize == 0 || secondSize == 0) {
                return false;
            }
            const uint64_t firstEnd = static_cast<uint64_t>(firstAddr) + firstSize;
            const uint64_t secondEnd = static_cast<uint64_t>(secondAddr) + secondSize;
            return firstAddr < secondEnd && secondAddr < firstEnd;
        }

        bool rangeContains(uint32_t outerAddr, uint32_t outerSize, uint32_t innerAddr, uint32_t innerSize)
        {
            if (outerSize == 0 || innerSize == 0) {
                return false;
            }
            const uint64_t outerEnd = static_cast<uint64_t>(outerAddr) + outerSize;
            const uint64_t innerEnd = static_cast<uint64_t>(innerAddr) + innerSize;
            return outerAddr <= innerAddr && innerEnd <= outerEnd;
        }

        bool eraseRange(const j2534::J2534Channel& channel, uint32_t canId, uint32_t startAddr,
                        uint32_t eraseLength, size_t retryCount)
        {
            const auto eraseAddr = toVector(startAddr);
            const auto eraseSize = toVector(eraseLength);
            UDSMessage eraseRoutineMsg(canId, { 0x31, 0x01, 0xff, 0x00,
                                                eraseAddr[0], eraseAddr[1], eraseAddr[2], eraseAddr[3],
                                                eraseSize[0], eraseSize[1], eraseSize[2], eraseSize[3] });
            unsigned long numMsgs;
            for(size_t i = 0; i < retryCount; ++i) {
                const auto writeStatus = channel.writeMsgs(eraseRoutineMsg, numMsgs);
                if (writeStatus != STATUS_NOERROR || numMsgs < 1) {
                    LOG(ERROR) << "eraseRange failed to write message, addr=0x" << std::hex << startAddr
                               << " length=0x" << eraseLength
                               << " status=" << j2534StatusToString(writeStatus)
                               << " written=" << std::dec << numMsgs;
                    continue;
                }
                const auto result{ readMessageCheckAndGet(channel, { 0x71, 0x01, 0xff, 0x00, 0x00 }, {}, 10) };
                if(result.empty()) {
                    LOG(WARNING) << "eraseRange got no positive response, addr=0x" << std::hex << startAddr
                                 << " length=0x" << eraseLength;
                    std::this_thread::sleep_for(std::chrono::milliseconds(500));
                    continue;
                }
                return true;
            }
            return false;
        }

        bool eraseBlockTouchesData(const EraseBlock& eraseBlock, const std::vector<VBFChunk>& chunks)
        {
            for (const auto& chunk: chunks) {
                if (rangesOverlap(eraseBlock.startAddr, eraseBlock.length, chunk.writeOffset,
                                  static_cast<uint32_t>(chunk.data.size()))) {
                    return true;
                }
            }
            return false;
        }

        bool chunkCoveredByEraseBlock(const std::vector<EraseBlock>& eraseBlocks, const VBFChunk& chunk)
        {
            for (const auto& eraseBlock: eraseBlocks) {
                if (rangeContains(eraseBlock.startAddr, eraseBlock.length, chunk.writeOffset,
                                  static_cast<uint32_t>(chunk.data.size()))) {
                    return true;
                }
            }
            return false;
        }

	}

	std::vector<std::unique_ptr<j2534::J2534Channel>> UDSProtocolCommonSteps::openChannels(
		j2534::J2534& j2534, unsigned long baudrate, uint32_t canId)
    {
        LOG(INFO) << "openChannels enter";
        std::vector<std::unique_ptr<j2534::J2534Channel>> result;
		result.emplace_back(openUDSChannel(j2534, baudrate, canId));
		result.emplace_back(openLowSpeedChannel(j2534, CAN_ID_BOTH));
        LOG(INFO) << "openChannels exit";
        return result;
	}

	bool UDSProtocolCommonSteps::fallAsleep(const std::vector<std::unique_ptr<j2534::J2534Channel>>& channels)
	{
        LOG(INFO) << "fallAsleep enter";
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
        LOG(INFO) << "fallAsleep exit";
        return true;
	}

	std::vector<unsigned long> UDSProtocolCommonSteps::keepAlive(const j2534::J2534Channel& channel)
	{
		return channel.startPeriodicMsgs(UDSMessage(0x7DF, { 0x3E, 0x80 }), 1900);
	}

	bool UDSProtocolCommonSteps::broadcastProgrammingMode(
		const std::vector<std::unique_ptr<j2534::J2534Channel>>& channels, unsigned long durationMs)
	{
        LOG(INFO) << "broadcastProgrammingMode enter";
        if (channels.empty()) {
            LOG(ERROR) << "broadcastProgrammingMode failed: no open channels";
            return false;
        }
        std::vector<std::vector<unsigned long>> msgIds(channels.size());
        bool success = true;
        for (size_t i = 0; i < channels.size(); ++i) {
            msgIds[i] = channels[i]->startPeriodicMsgs(UDSMessage(0x7DF, { 0x10, 0x82 }), 20);
            if (msgIds[i].empty()) {
                LOG(ERROR) << "broadcastProgrammingMode failed to start periodic message on channel " << i;
                success = false;
            }
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(durationMs));
        for (size_t i = 0; i < channels.size(); ++i) {
            if (!msgIds[i].empty()) {
                channels[i]->stopPeriodicMsg(msgIds[i]);
            }
        }
        LOG(INFO) << "broadcastProgrammingMode exit";
        return success;
	}

	void UDSProtocolCommonSteps::wakeUp(const std::vector<std::unique_ptr<j2534::J2534Channel>>& channels)
	{
        LOG(INFO) << "wakeUp enter";
        for(const auto& idToWakeUp: {0x11, 0x81}) {
            std::vector<std::vector<unsigned long>> msgIds(channels.size());
            for (size_t i = 0; i < channels.size(); ++i) {
                const auto ids = channels[i]->startPeriodicMsgs(UDSMessage(0x7DF, { 0x11, static_cast<uint8_t>(idToWakeUp) }), 20);
                if (ids.empty()) {
                    LOG(ERROR) << "wakeUp error, failed to start periodic messages on channel = " << i;
                    return;
                }
                msgIds[i] = ids;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(200));
            for (size_t i = 0; i < channels.size(); ++i) {
                channels[i]->stopPeriodicMsg(msgIds[i]);
            }
        }
        LOG(INFO) << "wakeUp exit";
        return;
	}

	bool UDSProtocolCommonSteps::authorize(const j2534::J2534Channel& channel, uint32_t canId,
		const std::array<uint8_t, 5>& pin)
	{
        LOG(INFO) << "authorize enter pin=" << pinToHexString(pin);
        UDSRequest seedRequest(canId, { 0x27, 0x01 });
        for(size_t i = 0; i < 5; ++i) {
            try {
                channel.clearRx();
                const auto seedResponse(seedRequest.process(channel));
                if (seedResponse.size() < 9)
                    return false;
                std::array<uint8_t, 3> seed = { seedResponse[6], seedResponse[7], seedResponse[8] };
                uint32_t key = generateKey(pin, seed);
                channel.clearRx();
                UDSRequest keyRequest(canId, { 0x27, 0x02, (key >> 16) & 0xFF, (key >> 8) & 0xFF, key & 0xFF });
                try {
                    const auto keyResponse(keyRequest.process(channel));
                    const bool result = keyResponse.size() >= 6 && keyResponse[5] == 0x02;
                    if(!result) {
                        LOG(ERROR) << "authorize wrong pin, pin=" << pinToHexString(pin);
                    }
                    else {
                        LOG(INFO) << "authorize success, pin=" << pinToHexString(pin);
                    }
                    return result;
                }
                catch(UDSError& error) {
                    if(error.getErrorCode() == UDSError::ErrorCode::RequiredTimeDelayHasNotExpired) {
                    }
                    LOG(ERROR) << "authorize error: " << error.what() << ", pin = "
                               << pinToHexString(pin);
                }
            }
            catch (...) {
                std::this_thread::sleep_for(std::chrono::seconds(5));
            }
        }
        LOG(INFO) << "authorization failed, pin=" << pinToHexString(pin);
        return false;
	}

    bool finishTransfer(const j2534::J2534Channel& channel, uint32_t canId, uint16_t expectedCrc)
    {
        UDSRequest transferExitRequest{ canId, { 0x37 } };
        const auto response = transferExitRequest.process(channel, 10000);
        if (response.size() < 5 || response[4] != 0x77) {
            LOG(ERROR) << "TransferExit got unexpected response: " << toHexString(response);
            return false;
        }
        if (response.size() >= 7) {
            const uint16_t returnedCrc = (static_cast<uint16_t>(response[5]) << 8) | response[6];
            if (returnedCrc != expectedCrc) {
                LOG(ERROR) << "TransferExit CRC mismatch, expected=0x" << std::hex << expectedCrc
                           << " actual=0x" << returnedCrc;
                return false;
            }
        }
        return true;
    }

    bool UDSProtocolCommonSteps::transferChunk(const j2534::J2534Channel& channel, uint32_t canId, const VBFChunk& chunk,
                                              const std::function<void(size_t)>& progressCallback)
	{
        LOG(INFO) << "transferChunk enter chunk: " << std::hex << chunk.writeOffset;
        try {
            const auto startAddr = chunk.writeOffset;
            const auto dataSize = chunk.data.size();
            UDSRequest requestDownloadRequest{ canId, { 0x34, 0x00, 0x44,
                (startAddr >> 24) & 0xFF, (startAddr >> 16) & 0xFF, (startAddr >> 8) & 0xFF, startAddr & 0xFF,
                (dataSize >> 24) & 0xFF, (dataSize >> 16) & 0xFF, (dataSize >> 8) & 0xFF, dataSize & 0xFF } };
            const auto downloadResponse{ requestDownloadRequest.process(channel, { 0x20 }, 10) };
            if (downloadResponse.size() < 2) {
                return false;
            }
            const size_t maxSizeToTransfer = encodeBigEndian(downloadResponse[1], downloadResponse[0]) - 2;
            uint8_t chunkIndex = 1;
            for (size_t i = 0; i < chunk.data.size(); i += maxSizeToTransfer, ++chunkIndex) {
                const auto chunkEnd{ std::min(i + maxSizeToTransfer, chunk.data.size()) };
                LOG(INFO) << "transferChunk write chunk: {" << std::hex << i << ", " << chunkEnd <<"}";
                std::vector<uint8_t> data{ 0x36, chunkIndex };
                data.insert(data.end(), chunk.data.cbegin() + i, chunk.data.cbegin() + chunkEnd);
                UDSRequest transferDataRequest{ canId, std::move(data) };
                transferDataRequest.process(channel, { chunkIndex }, 10, 60000);
                progressCallback(chunkEnd - i);
            }
            LOG(INFO) << "transferChunk finish transfer, crc: {" << std::hex
                      << ((chunk.crc >> 8) & 0xFF) << ", " << (chunk.crc & 0xFF) <<"}";
            if (!finishTransfer(channel, canId, chunk.crc)) {
                return false;
            }
		}
        catch(const std::exception& ex) {
            LOG(ERROR) << "transferChunk error, ex = " << ex.what() << ", offset = " << std::hex << chunk.writeOffset;
            return false;
        }
        catch (...) {
            LOG(ERROR) << "transferChunk error: offset = " << std::hex << chunk.writeOffset;
            return false;
        }
        LOG(INFO) << "transferChunk completed, offset = " << std::hex << chunk.writeOffset;
		return true;
	}

    bool UDSProtocolCommonSteps::transferData(const j2534::J2534Channel& channel, uint32_t canId, const VBF& data,
                                              const std::function<void(size_t)>& progressCallback)
    {
        LOG(INFO) << "transferData enter";
        try {
            for (const auto& chunk : data.chunks) {
                const auto startAddr = chunk.writeOffset;
                const auto dataSize = chunk.data.size();
                UDSRequest requestDownloadRequest{ canId, { 0x34, 0x00, 0x44,
                                                          (startAddr >> 24) & 0xFF, (startAddr >> 16) & 0xFF, (startAddr >> 8) & 0xFF, startAddr & 0xFF,
                                                          (dataSize >> 24) & 0xFF, (dataSize >> 16) & 0xFF, (dataSize >> 8) & 0xFF, dataSize & 0xFF } };
                const auto downloadResponse{ requestDownloadRequest.process(channel, { 0x20 }, 10) };
                if (downloadResponse.size() < 2) {
                    return false;
                }
                const size_t maxSizeToTransfer = encodeBigEndian(downloadResponse[1], downloadResponse[0]) - 2;
                uint8_t chunkIndex = 1;
                for (size_t i = 0; i < chunk.data.size(); i += maxSizeToTransfer, ++chunkIndex) {
                    const auto chunkEnd{ std::min(i + maxSizeToTransfer, chunk.data.size()) };
                    std::vector<uint8_t> data{ 0x36, chunkIndex };
                    data.insert(data.end(), chunk.data.cbegin() + i, chunk.data.cbegin() + chunkEnd);
                    UDSRequest transferDataRequest{ canId, std::move(data) };
                    transferDataRequest.process(channel, { chunkIndex }, 10, 60000);
                    progressCallback(chunkEnd - i);
                }
                if (!finishTransfer(channel, canId, chunk.crc)) {
                    return false;
                }
            }
        }
        catch(const std::exception& ex) {
            LOG(ERROR) << "transferData error, ex = " << ex.what();
            return false;
        }
        catch (...) {
            LOG(ERROR) << "transferData error";
            return false;
        }
        LOG(INFO) << "transferData completed";

        return true;
    }

    bool finishUpload(const j2534::J2534Channel& channel, uint32_t canId)
    {
        UDSRequest transferExitRequest{ canId, { 0x37 } };
        const auto response = transferExitRequest.process(channel, 10000);
        if (response.size() < 5 || response[4] != 0x77) {
            LOG(ERROR) << "Upload TransferExit got unexpected response: " << toHexString(response);
            return false;
        }
        return true;
    }

    bool finishUploadBestEffort(const j2534::J2534Channel& channel, uint32_t canId)
    {
        try {
            return finishUpload(channel, canId);
        }
        catch (const std::exception& ex) {
            LOG(WARNING) << "Upload TransferExit best-effort cleanup failed: " << ex.what();
        }
        catch (...) {
            LOG(WARNING) << "Upload TransferExit best-effort cleanup failed";
        }
        return false;
    }

    bool UDSProtocolCommonSteps::readDataByUpload(const j2534::J2534Channel& channel, uint32_t canId,
                                                  uint32_t startAddr, uint32_t dataSize,
                                                  std::vector<uint8_t>& output,
                                                  const std::function<void(size_t)>& progressCallback)
    {
        LOG(INFO) << "readDataByUpload enter addr=0x" << std::hex << startAddr
                  << " size=0x" << dataSize;
        if (dataSize == 0) {
            output.clear();
            LOG(INFO) << "readDataByUpload completed empty range";
            return true;
        }
        bool uploadStarted = false;
        try {
            output.clear();
            output.reserve(dataSize);

            UDSRequest requestUploadRequest{ canId, { 0x35, 0x00, 0x44,
                (startAddr >> 24) & 0xFF, (startAddr >> 16) & 0xFF, (startAddr >> 8) & 0xFF, startAddr & 0xFF,
                (dataSize >> 24) & 0xFF, (dataSize >> 16) & 0xFF, (dataSize >> 8) & 0xFF, dataSize & 0xFF } };
            const auto uploadResponse = requestUploadRequest.process(channel, 10000);
            if (uploadResponse.size() < 8 || uploadResponse[4] != 0x75) {
                LOG(ERROR) << "RequestUpload got unexpected response: " << toHexString(uploadResponse);
                return false;
            }
            uploadStarted = true;

            if (uploadResponse[5] != 0x20) {
                LOG(ERROR) << "RequestUpload unsupported length format: 0x"
                           << std::hex << static_cast<int>(uploadResponse[5]);
                finishUploadBestEffort(channel, canId);
                return false;
            }

            size_t maxBlockSize = (static_cast<size_t>(uploadResponse[6]) << 8) | uploadResponse[7];
            if (maxBlockSize <= 2) {
                LOG(ERROR) << "RequestUpload invalid max block size: 0x" << std::hex << maxBlockSize;
                finishUploadBestEffort(channel, canId);
                return false;
            }
            if (dataSize < maxBlockSize) {
                maxBlockSize = static_cast<size_t>(dataSize) + 2;
            }
            const size_t maxPayloadSize = maxBlockSize - 2;
            LOG(INFO) << "readDataByUpload maxBlockSize=0x" << std::hex << maxBlockSize
                      << " maxPayloadSize=0x" << maxPayloadSize;

            std::this_thread::sleep_for(std::chrono::milliseconds(30));
            uint8_t blockIndex = 1;
            while (output.size() < dataSize) {
                bool blockRead = false;
                for (size_t attempt = 1; attempt <= 3 && !blockRead; ++attempt) {
                    UDSRequest transferDataRequest{ canId, { 0x36, blockIndex } };
                    const auto transferResponse = transferDataRequest.process(channel, { blockIndex }, 10, 60000);
                    if (transferResponse.empty()) {
                        LOG(WARNING) << "TransferData upload returned empty payload, block=0x"
                                     << std::hex << static_cast<int>(blockIndex)
                                     << " attempt=" << std::dec << attempt;
                        std::this_thread::sleep_for(std::chrono::milliseconds(30));
                        continue;
                    }
                    const size_t remaining = dataSize - output.size();
                    if (transferResponse.size() > maxPayloadSize) {
                        LOG(ERROR) << "TransferData upload block too large, block=0x"
                                   << std::hex << static_cast<int>(blockIndex)
                                   << " size=0x" << transferResponse.size()
                                   << " max=0x" << maxPayloadSize;
                        finishUploadBestEffort(channel, canId);
                        return false;
                    }
                    if (transferResponse.size() > remaining) {
                        LOG(ERROR) << "TransferData upload returned more data than requested, block=0x"
                                   << std::hex << static_cast<int>(blockIndex)
                                   << " size=0x" << transferResponse.size()
                                   << " remaining=0x" << remaining;
                        finishUploadBestEffort(channel, canId);
                        return false;
                    }
                    const size_t bytesToCopy = transferResponse.size();
                    output.insert(output.end(), transferResponse.cbegin(), transferResponse.cbegin() + bytesToCopy);
                    progressCallback(bytesToCopy);
                    blockRead = true;
                }
                if (!blockRead) {
                    LOG(ERROR) << "TransferData upload failed, block=0x"
                               << std::hex << static_cast<int>(blockIndex);
                    finishUploadBestEffort(channel, canId);
                    return false;
                }
                ++blockIndex;
            }

            if (output.size() != dataSize) {
                LOG(ERROR) << "readDataByUpload size mismatch expected=0x" << std::hex << dataSize
                           << " actual=0x" << output.size();
                finishUploadBestEffort(channel, canId);
                return false;
            }
            if (!finishUpload(channel, canId)) {
                return false;
            }
            uploadStarted = false;
        }
        catch(const std::exception& ex) {
            LOG(ERROR) << "readDataByUpload error, ex = " << ex.what()
                       << ", addr = 0x" << std::hex << startAddr;
            if (uploadStarted) {
                finishUploadBestEffort(channel, canId);
            }
            return false;
        }
        catch (...) {
            LOG(ERROR) << "readDataByUpload error, addr = 0x" << std::hex << startAddr;
            if (uploadStarted) {
                finishUploadBestEffort(channel, canId);
            }
            return false;
        }
        LOG(INFO) << "readDataByUpload completed addr=0x" << std::hex << startAddr
                  << " size=0x" << dataSize;
        return true;
    }

    bool UDSProtocolCommonSteps::eraseFlash(const j2534::J2534Channel& channel, uint32_t canId, const VBF& data)
    {
        LOG(INFO) << "eraseFlash enter";
        std::set<std::pair<uint32_t, uint32_t>> erasedBlocks;
        for (const auto& eraseBlock : data.header.eraseBlocks) {
            if (eraseBlock.length == 0 || !eraseBlockTouchesData(eraseBlock, data.chunks)) {
                continue;
            }
            const auto blockKey = std::make_pair(eraseBlock.startAddr, eraseBlock.length);
            if (!erasedBlocks.insert(blockKey).second) {
                continue;
            }
            LOG(INFO) << "eraseFlash erase block: " << std::hex << eraseBlock.startAddr
                      << ", length = " << eraseBlock.length;
            if (!eraseRange(channel, canId, eraseBlock.startAddr, eraseBlock.length, 10)) {
                LOG(ERROR) << "Failed to erase block: " << std::hex << eraseBlock.startAddr;
                return false;
            }
        }
        for (const auto& chunk : data.chunks) {
            if (chunk.data.empty() || chunkCoveredByEraseBlock(data.header.eraseBlocks, chunk)) {
                continue;
            }
            if (!eraseRange(channel, canId, chunk.writeOffset, static_cast<uint32_t>(chunk.data.size()), 10)) {
                LOG(ERROR) << "Failed to erase chunk: " << std::hex << chunk.writeOffset;
                return false;
            }
        }
        LOG(INFO) << "eraseFlash completed";
        return true;
	}

    bool UDSProtocolCommonSteps::eraseChunk(const j2534::J2534Channel& channel, uint32_t canId, const VBFChunk& chunk)
    {
        LOG(INFO) << "eraseChunk enter chunk: " << std::hex << chunk.writeOffset;
        if (chunk.data.empty()) {
            LOG(INFO) << "eraseChunk skipped empty chunk, offset = " << std::hex << chunk.writeOffset;
            return true;
        }
        if (eraseRange(channel, canId, chunk.writeOffset, static_cast<uint32_t>(chunk.data.size()), 1)) {
            LOG(INFO) << "eraseChunk completed, offset = " << std::hex << chunk.writeOffset;
            return true;
        }
        LOG(ERROR) << "Failed to erase chunk: " << std::hex << chunk.writeOffset;
        return false;
    }

    bool UDSProtocolCommonSteps::startRoutine(const j2534::J2534Channel& channel, uint32_t canId, uint32_t addr)
    {
        LOG(INFO) << "startRoutine enter, addr = " << std::hex << addr;
        const auto callAddr = common::toVector(addr);
        common::UDSMessage startRoutineMsg(canId, { 0x31, 0x01, 0x03, 0x01, callAddr[0], callAddr[1], callAddr[2], callAddr[3] });
        unsigned long numMsgs;
        const auto writeStatus = channel.writeMsgs(startRoutineMsg, numMsgs);
        if (writeStatus != STATUS_NOERROR || numMsgs < 1) {
            LOG(ERROR) << "startRoutine failed to write message, addr = " << std::hex << addr
                       << " status=" << j2534StatusToString(writeStatus)
                       << " written=" << std::dec << numMsgs;
            return false;
        }
        if (!common::readMessageAndCheck(channel, { 0x71, 0x01, 0x03, 0x01 }, {}, 10)) {
            LOG(ERROR) << "startRoutine failed to read message, addr = " << std::hex << addr;
            return false;
        }
        LOG(INFO) << "startRoutine completed, addr = " << std::hex << addr;
        return true;
    }

    bool UDSProtocolCommonSteps::checkValidApplication(const j2534::J2534Channel& channel, uint32_t canId)
    {
        LOG(INFO) << "checkValidApplication enter";
        UDSRequest checkValidApplicationRequest{ canId, { 0x31, 0x01, 0x03, 0x04 } };
        try {
            checkValidApplicationRequest.process(channel);
        }
        catch(const std::exception& ex) {
            LOG(ERROR) << "checkValidApplication error, ex = " << ex.what();
            return false;
        }
        LOG(INFO) << "checkValidApplication finshed";
        return true;
    }

} // namespace common
