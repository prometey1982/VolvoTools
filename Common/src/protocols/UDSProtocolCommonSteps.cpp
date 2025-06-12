#include "common/protocols/UDSProtocolCommonSteps.hpp"

#include "common/protocols/UDSRequest.hpp"
#include "common/protocols/UDSError.hpp"
#include "common/Util.hpp"

#include <easylogging++.h>

#include <thread>

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
        LOG(INFO) << "authorize enter pin: "<< std::hex << pin[0] << pin[1] << pin[2] << pin[3] << pin[4];
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
                        LOG(ERROR) << "authorize wrong pin, pin: "<< std::hex << pin[0] << pin[1] << pin[2] << pin[3] << pin[4];
                    }
                    else {
                        LOG(INFO) << "authorize success, pin: "<< std::hex << pin[0] << pin[1] << pin[2] << pin[3] << pin[4];
                    }
                    return result;
                }
                catch(UDSError& error) {
                    if(error.getErrorCode() == UDSError::ErrorCode::RequiredTimeDelayHasNotExpired) {
                    }
                    LOG(ERROR) << "authorize error: " << error.what() << ", pin = "
                               << std::hex << pin[0] << pin[1] << pin[2] << pin[3] << pin[4];
                }
            }
            catch (...) {
                std::this_thread::sleep_for(std::chrono::seconds(5));
            }
        }
        LOG(INFO) << "authorization failed, pin: "<< std::hex << pin[0] << pin[1] << pin[2] << pin[3] << pin[4];
        return false;
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
            unsigned long numMsgs;
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
            UDSRequest transferExitRequest{ canId, { 0x37 } };
            transferExitRequest.process(
                channel, { static_cast<uint8_t>(chunk.crc >> 8), static_cast<uint8_t>(chunk.crc) }, 3, 10000);
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
                unsigned long numMsgs;
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
                UDSRequest transferExitRequest{ canId, { 0x37 } };
                transferExitRequest.process(
                    channel, { static_cast<uint8_t>(chunk.crc >> 8), static_cast<uint8_t>(chunk.crc) }, 3, 10000);
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

    bool UDSProtocolCommonSteps::eraseFlash(const j2534::J2534Channel& channel, uint32_t canId, const VBF& data)
    {
        LOG(INFO) << "eraseFlash enter";
        for (const auto& chunk : data.chunks) {
			const auto eraseAddr = toVector(chunk.writeOffset);
			const auto eraseSize = toVector(static_cast<uint32_t>(chunk.data.size()));
			UDSMessage eraseRoutineMsg(canId, { 0x31, 0x01, 0xff, 0x00,
				eraseAddr[0], eraseAddr[1], eraseAddr[2], eraseAddr[3],
				eraseSize[0], eraseSize[1], eraseSize[2], eraseSize[3] });
			unsigned long numMsgs;
            for(size_t i = 0; i < 10; ++i) {
                if (channel.writeMsgs(eraseRoutineMsg, numMsgs) != STATUS_NOERROR || numMsgs < 1) {
                    continue;
                }
                const auto result{ readMessageCheckAndGet(channel, { 0x71, 0x01, 0xff, 0x00, 0x00 }, {}, 10) };
                if(result.empty()) {
                    std::this_thread::sleep_for(std::chrono::milliseconds(500));
                    continue;
                }
                return true;
                LOG(INFO) << "eraseFlash completed";
            }
		}
        LOG(ERROR) << "Failed to erase data";
        return false;
	}

    bool UDSProtocolCommonSteps::eraseChunk(const j2534::J2534Channel& channel, uint32_t canId, const VBFChunk& chunk)
    {
        LOG(INFO) << "eraseChunk enter chunk: " << std::hex << chunk.writeOffset;
        const auto eraseAddr = toVector(chunk.writeOffset);
        const auto eraseSize = toVector(static_cast<uint32_t>(chunk.data.size()));
        UDSMessage eraseRoutineMsg(canId, { 0x31, 0x01, 0xff, 0x00,
                                           eraseAddr[0], eraseAddr[1], eraseAddr[2], eraseAddr[3],
                                           eraseSize[0], eraseSize[1], eraseSize[2], eraseSize[3] });
        unsigned long numMsgs;
        for(size_t i = 0; i < 1; ++i) {
            if (channel.writeMsgs(eraseRoutineMsg, numMsgs) != STATUS_NOERROR || numMsgs < 1) {
                continue;
            }
            const auto result{ readMessageCheckAndGet(channel, { 0x71, 0x01, 0xff, 0x00, 0x00 }, {}, 10) };
            if(result.empty()) {
                std::this_thread::sleep_for(std::chrono::milliseconds(500));
                continue;
            }
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
        if (channel.writeMsgs(startRoutineMsg, numMsgs) != STATUS_NOERROR || numMsgs < 1) {
            LOG(ERROR) << "startRoutine failed to write message, addr = " << std::hex << addr;
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
