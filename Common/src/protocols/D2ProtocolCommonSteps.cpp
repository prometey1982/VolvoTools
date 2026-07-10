#include "common/protocols/D2ProtocolCommonSteps.hpp"

#include "common/ICanChannel.hpp"
#include "common/Util.hpp"
#include "common/protocols/D2Message.hpp"
#include "common/protocols/D2Messages.hpp"

#define LOG_MODULE_NAME "common"
#include "common/LogHelper.hpp"

#include <map>
#include <stdexcept>
#include <thread>
#include <chrono>

namespace common {

namespace {

    CanFrame makeBootloaderFrame(uint8_t ecuId, const std::vector<uint8_t>& data)
    {
        std::vector<uint8_t> payload(8, 0);
        payload[0] = ecuId;
        const auto copySize = std::min(data.size(), static_cast<size_t>(7));
        std::copy(data.begin(), data.begin() + copySize, payload.begin() + 1);
        return { D2Message::CanId, std::move(payload), true };
    }

    bool writeMessagesAndCheckAnswer(ICanChannel& channel,
                                     const CanFrame& message,
                                     const std::vector<uint8_t>& toCheck,
                                     size_t count = 10)
    {
        if (!channel.send(message)) {
            throw std::runtime_error("write msgs error");
        }
        for (size_t i = 0; i < count; ++i) {
            CanFrame received;
            if (!channel.receive(received, 3000)) {
                continue;
            }
            if (received.data.size() < 1 + toCheck.size()) {
                continue;
            }
            bool success = true;
            for (size_t j = 0; j < toCheck.size(); ++j) {
                if (toCheck[j] != received.data[1 + j]) {
                    success = false;
                    break;
                }
            }
            if (success) {
                return true;
            }
        }
        return false;
    }

    bool writeMessagesAndCheckAnswer(ICanChannel& channel,
                                     const CanFrame& message,
                                     const std::vector<std::vector<uint8_t>>& toChecks,
                                     size_t count = 10)
    {
        if (!channel.send(message)) {
            throw std::runtime_error("write msgs error");
        }
        for (size_t i = 0; i < count; ++i) {
            CanFrame received;
            if (!channel.receive(received, 3000)) {
                continue;
            }
            for (const auto& toCheck : toChecks) {
                if (received.data.size() < 1 + toCheck.size()) {
                    continue;
                }
                bool success = true;
                for (size_t j = 0; j < toCheck.size(); ++j) {
                    if (toCheck[j] != received.data[1 + j]) {
                        success = false;
                        break;
                    }
                }
                if (success) {
                    return true;
                }
            }
        }
        return false;
    }

    void writeDataOffsetAndCheckAnswer(ICanChannel& channel, uint8_t ecuId, uint32_t writeOffset)
    {
        const auto addrBytes = toVector(writeOffset);
        const auto msg = makeBootloaderFrame(ecuId, {0x9C, addrBytes[0], addrBytes[1], addrBytes[2], addrBytes[3]});
        for (int i = 0; i < 10; ++i) {
            if (writeMessagesAndCheckAnswer(channel, msg, { 0x9C }))
                return;
            std::this_thread::sleep_for(std::chrono::seconds(1));
        }
        throw std::runtime_error("CM didn't response with correct answer");
    }

    uint8_t calculateCheckSum(const std::vector<uint8_t> &bin, size_t beginOffset,
                              size_t endOffset) {
        uint32_t sum = 0;
        for (size_t i = beginOffset; i < endOffset; ++i) {
            sum += bin[i];
        }
        do {
            sum = ((sum >> 24) & 0xFF) + ((sum >> 16) & 0xFF) + ((sum >> 8) & 0xFF) +
                  (sum & 0xFF);
        } while (((sum >> 8) & 0xFFFFFF) != 0);
        return static_cast<uint8_t>(sum);
    }

    std::vector<std::vector<CanFrame>> createWriteDataFrames(uint8_t ecuId,
                                                              const std::vector<uint8_t>& data,
                                                              size_t beginOffset,
                                                              size_t endOffset)
    {
        std::vector<std::vector<CanFrame>> result;
        const size_t chunkSize = 6;
        const size_t maxFramesPerBatch = 10;

        for (size_t i = beginOffset; i < endOffset; i += chunkSize) {
            const auto payloadSize = std::min(chunkSize, endOffset - i);
            std::vector<uint8_t> payload(8, 0);
            payload[0] = ecuId;
            payload[1] = 0xA8 + static_cast<uint8_t>(payloadSize);
            std::copy(data.begin() + i, data.begin() + i + payloadSize, payload.begin() + 2);

            if (result.empty() || result.back().size() >= maxFramesPerBatch) {
                result.emplace_back();
            }
            result.back().emplace_back(D2Message::CanId, std::move(payload), true);
        }
        return result;
    }

}

    bool D2ProtocolCommonSteps::fallAsleep(const std::vector<std::unique_ptr<ICanChannel>>& channels)
	{
        LOG_MODULE(TRACE) << "fallAsleep enter";
        for (size_t i = 0; i < channels.size(); ++i) {
            unsigned long msgId;
            if (!channels[i]->startPeriodicMsg({D2Message::CanId, {0xFF, 0x86, 0, 0, 0, 0, 0, 0}, true}, 5, msgId)) {
                return false;
            }
            std::this_thread::sleep_for(std::chrono::seconds(3));
            channels[i]->stopPeriodicMsg(msgId);
        }
        LOG_MODULE(TRACE) << "fallAsleep exit";
        return true;
	}

    bool D2ProtocolCommonSteps::startPBL(ICanChannel& channel, uint8_t ecuId)
    {
        LOG_MODULE(TRACE) << "startPBL enter";
        if (!writeMessagesAndCheckAnswer(
                channel,
                makeBootloaderFrame(ecuId, {0xC0}),
                { 0xC6 })) {
            throw std::runtime_error("CM didn't response with correct answer");
        }
        LOG_MODULE(TRACE) << "startPBL exit";
        return true;
    }

    void D2ProtocolCommonSteps::wakeUp(const std::vector<std::unique_ptr<ICanChannel>>& channels)
    {
        LOG_MODULE(TRACE) << "wakeUp enter";
        for (size_t i = 0; i < channels.size(); ++i) {
            channels[i]->send({D2Message::CanId, {0xFF, 0xC8, 0, 0, 0, 0, 0, 0}, true});
        }
        LOG_MODULE(TRACE) << "wakeUp exit";
    }

    bool D2ProtocolCommonSteps::transferData(ICanChannel& channel, uint8_t ecuId, const VBF& data,
                                             const std::function<void(size_t)>& progressCallback)
	{
        LOG_MODULE(TRACE) << "transferData enter";
        for(const auto& chunk: data.chunks) {
            auto batches = createWriteDataFrames(ecuId, chunk.data, 0, chunk.data.size());

        writeDataOffsetAndCheckAnswer(channel, ecuId, chunk.writeOffset);
        for (const auto& batch : batches) {
            channel.clearRx();
            if (!channel.send(batch, 50000)) {
                throw std::runtime_error("write msgs error");
            }
            progressCallback(6 * batch.size());
        }
        writeDataOffsetAndCheckAnswer(channel, ecuId, chunk.writeOffset);
        uint32_t endOffset =  chunk.writeOffset + chunk.data.size();
        uint8_t checksum = calculateCheckSum(chunk.data, 0, chunk.data.size());
        if (!writeMessagesAndCheckAnswer(
                channel,
                makeBootloaderFrame(ecuId, {0xB4, static_cast<uint8_t>((endOffset >> 24) & 0xFF),
                                              static_cast<uint8_t>((endOffset >> 16) & 0xFF),
                                              static_cast<uint8_t>((endOffset >> 8) & 0xFF),
                                              static_cast<uint8_t>(endOffset & 0xFF)}),
                { 0xB1, checksum }))
            throw std::runtime_error("Failed. Checksums are not equal.");
        }
        LOG_MODULE(TRACE) << "transferData exit";
        return true;
    }

    bool D2ProtocolCommonSteps::eraseFlash(ICanChannel& channel, uint8_t ecuId, const VBF& data)
    {
        LOG_MODULE(TRACE) << "eraseFlash enter";
        for (const auto& chunk : data.chunks) {
            writeDataOffsetAndCheckAnswer(channel, ecuId, chunk.writeOffset);
            std::this_thread::sleep_for(std::chrono::milliseconds(1000));
            if (!writeMessagesAndCheckAnswer(
                    channel,
                    makeBootloaderFrame(ecuId, {0xF8}),
                    {{ 0xF9, 0x0 }, { 0xF9, 0x2 }}, 30))
                throw std::runtime_error("Can't erase memory");
        }
        LOG_MODULE(TRACE) << "eraseFlash exit";
        return true;
    }

    void D2ProtocolCommonSteps::jumpTo(ICanChannel& channel, uint8_t ecuId, uint32_t addr)
    {
        LOG_MODULE(TRACE) << "jumpTo enter addr: " << std::hex << addr;
        writeDataOffsetAndCheckAnswer(channel, ecuId, addr);
        LOG_MODULE(TRACE) << "jumpTo exit";
    }

    bool D2ProtocolCommonSteps::startRoutine(ICanChannel& channel, uint8_t ecuId, uint32_t addr)
	{
        LOG_MODULE(TRACE) << "startRoutine enter addr: " << std::hex << addr;
        writeDataOffsetAndCheckAnswer(channel, ecuId, addr);
        if (!writeMessagesAndCheckAnswer(
                channel,
                makeBootloaderFrame(ecuId, {0xA0}),
                { 0xA0 })) {
            throw std::runtime_error("Can't start routine");
        }
        LOG_MODULE(TRACE) << "startRoutine exit";
        return true;
    }

    void D2ProtocolCommonSteps::setDIMTime(const std::vector<std::unique_ptr<ICanChannel>>& channels)
    {
        LOG_MODULE(TRACE) << "setDIMTime enter";
        std::this_thread::sleep_for(std::chrono::seconds(1));
        const auto now{std::chrono::system_clock::now()};
        const auto time_t = std::chrono::system_clock::to_time_t(now);
        struct tm lt;
        localtime_s(&lt, &time_t);

        const auto msg = D2Messages::setCurrentTime(
            static_cast<uint8_t>(lt.tm_hour),
            static_cast<uint8_t>(lt.tm_min));
        for(const auto& channel: channels) {
            if(channel->getBaudrate() == 125000) {
                channel->send(msg.getFrames());
            }
        }
        LOG_MODULE(TRACE) << "setDIMTime exit";
    }

} // namespace common
