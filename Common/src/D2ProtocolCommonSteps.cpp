#include "common/D2ProtocolCommonSteps.hpp"

#include "common/D2Request.hpp"
#include "common/D2Messages.hpp"
#include "common/Util.hpp"

#include <map>
#include <stdexcept>
#include <thread>

namespace common {

namespace {

    bool writeMessagesAndCheckAnswer(const j2534::J2534Channel& channel,
                                     const j2534::BaseMessage& message,
                                     const std::vector<uint8_t>& toCheck, size_t count = 10)
    {
        unsigned long msgsNum = 1;
        const auto error = channel.writeMsgs(message, msgsNum, 5000);
        if (error != STATUS_NOERROR) {
            throw std::runtime_error("write msgs error");
        }
        for (size_t i = 0; i < count; ++i) {
            std::vector<PASSTHRU_MSG> received_msgs(1);
            channel.readMsgs(received_msgs, 3000);
            for (const auto &msg : received_msgs) {
                bool success = true;
                for(size_t i = 0; i < toCheck.size(); ++i) {
                    if(toCheck[i] != msg.Data[i + 5]) {
                        success = false;
                        break;
                    }
                }
                if(success) {
                    return true;
                }
            }
        }
        return false;
    }

    bool writeMessagesAndCheckAnswer(const j2534::J2534Channel& channel,
                                     const j2534::BaseMessage& message,
                                     const std::vector<std::vector<uint8_t>>& toChecks, size_t count = 10)
    {
        unsigned long msgsNum = 1;
        const auto error = channel.writeMsgs(message, msgsNum, 5000);
        if (error != STATUS_NOERROR) {
            throw std::runtime_error("write msgs error");
        }
        for (size_t i = 0; i < count; ++i) {
            std::vector<PASSTHRU_MSG> received_msgs(1);
            channel.readMsgs(received_msgs, 3000);
            for (const auto &msg : received_msgs) {
                for(const auto& toCheck: toChecks) {
                    bool success = true;
                    for(size_t i = 0; i < toCheck.size(); ++i) {
                        if(toCheck[i] != msg.Data[i + 5]) {
                            success = false;
                            break;
                        }
                    }
                    if(success) {
                        return true;
                    }
                }
            }
        }
        return false;
    }


    void writeDataOffsetAndCheckAnswer(const j2534::J2534Channel& channel, uint8_t ecuId, uint32_t writeOffset)
    {
        const auto writeOffsetMsgs{ common::D2Messages::createSetMemoryAddrMsg(ecuId, writeOffset) };
        for (int i = 0; i < 10; ++i) {
            if (writeMessagesAndCheckAnswer(channel, writeOffsetMsgs, { 0x9C }))
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

}

    bool D2ProtocolCommonSteps::fallAsleep(const std::vector<std::unique_ptr<j2534::J2534Channel>>& channels)
	{
        std::map<size_t, std::vector<unsigned long>> msgIds;
        for (size_t i = 0; i < channels.size(); ++i) {
            if (channels[i]->getProtocolId() != ISO9141) {
                msgIds[i] = channels[i]->startPeriodicMsgs(
                    common::D2Messages::goToSleepCanRequest, 5);
            }
        }
        std::this_thread::sleep_for(std::chrono::seconds(3));
        for (const auto& ids : msgIds) {
            channels[ids.first]->stopPeriodicMsg(ids.second);
        }
        return true;
	}

    bool D2ProtocolCommonSteps::startPBL(const j2534::J2534Channel& channel, uint8_t ecuId)
    {
        if (!writeMessagesAndCheckAnswer(
                channel,
                common::D2Messages::createStartPrimaryBootloaderMsg(ecuId),
                { 0xC6 })) {
            throw std::runtime_error("CM didn't response with correct answer");
        }
        return true;
    }

    void D2ProtocolCommonSteps::wakeUp(const std::vector<std::unique_ptr<j2534::J2534Channel>>& channels)
	{
        unsigned long numMsgs = 1;
        for (size_t i = 0; i < channels.size(); ++i) {
            if (channels[i]->getProtocolId() != ISO9141) {
                channels[i]->writeMsgs(common::D2Messages::wakeUpCanRequest, numMsgs, 5000);
            }
        }
    }

    bool D2ProtocolCommonSteps::transferData(const j2534::J2534Channel& channel, uint8_t ecuId, const VBF& data,
                                             const std::function<void(size_t)> progressCallback)
	{
        for(const auto& chunk: data.chunks) {
            auto binMsgs = common::D2Messages::createWriteDataMsgs(
                ecuId, chunk.data, 0, chunk.data.size());

        writeDataOffsetAndCheckAnswer(channel, ecuId, chunk.writeOffset);
        for (const auto binMsg : binMsgs) {
            channel.clearRx();
            const auto passThruMsgs = binMsg.toPassThruMsgs(channel.getProtocolId(), channel.getTxFlags());
            unsigned long msgsNum = passThruMsgs.size();
            const auto error = channel.writeMsgs(passThruMsgs, msgsNum, 50000);
            if (error != STATUS_NOERROR) {
                throw std::runtime_error("write msgs error");
            }
            progressCallback(6 * msgsNum);
        }
        writeDataOffsetAndCheckAnswer(channel, ecuId, chunk.writeOffset);
        uint32_t endOffset =  chunk.writeOffset + chunk.data.size();
        uint8_t checksum = calculateCheckSum(chunk.data, 0, chunk.data.size());
        if (!writeMessagesAndCheckAnswer(
                channel,
                common::D2Messages::createCalculateChecksumMsg(ecuId, endOffset),
                { 0xB1, checksum }))
            throw std::runtime_error("Failed. Checksums are not equal.");
        }
        return true;
    }

    bool D2ProtocolCommonSteps::eraseFlash(const j2534::J2534Channel& channel, uint8_t ecuId, const VBF& data)
	{
        for (const auto& chunk : data.chunks) {
            writeDataOffsetAndCheckAnswer(channel, ecuId, chunk.writeOffset);
            std::this_thread::sleep_for(std::chrono::milliseconds(1000));
            if (!writeMessagesAndCheckAnswer(
                    channel,
                    common::D2Messages::createEraseMsg(ecuId),
                    {{ 0xF9, 0x0 }, { 0xF9, 0x2 }}, 30))
                throw std::runtime_error("Can't erase memory");
//            std::this_thread::sleep_for(std::chrono::seconds(1));
        }
        return true;
    }

    void D2ProtocolCommonSteps::jumpTo(const j2534::J2534Channel& channel, uint8_t ecuId, uint32_t addr)
    {
        writeDataOffsetAndCheckAnswer(channel, ecuId, addr);
    }

    bool D2ProtocolCommonSteps::startRoutine(const j2534::J2534Channel& channel, uint8_t ecuId, uint32_t addr)
	{
        writeDataOffsetAndCheckAnswer(channel, ecuId, addr);
        if (!writeMessagesAndCheckAnswer(
                channel,
                common::D2Messages::createJumpToMsg(ecuId),
                { 0xA0 })) {
            throw std::runtime_error("Can't start routine");
        }
        return true;
	}

} // namespace common
