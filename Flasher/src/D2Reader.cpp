#include "flasher/D2Reader.hpp"

#include <common/Util.hpp>
#include <common/D2Message.hpp>
#include <common/SBL.hpp>
#include <common/VBFParser.hpp>
#include <j2534/J2534.hpp>
#include <j2534/J2534Channel.hpp>

#include <algorithm>
#include <numeric>
#include <time.h>

namespace {

    PASSTHRU_MSG writeMessagesAndReadMessage(j2534::J2534Channel& channel,
        const std::vector<PASSTHRU_MSG>& msgs) {
        channel.clearRx();
        unsigned long msgsNum = msgs.size();
        const auto error = channel.writeMsgs(msgs, msgsNum, 5000);
        if (error != STATUS_NOERROR) {
            throw std::runtime_error("write msgs error");
        }
        std::vector<PASSTHRU_MSG> received_msgs(1);
        channel.readMsgs(received_msgs, 3000);
        return received_msgs[0];
    }

} // namespace

namespace flasher {
D2Reader::D2Reader(j2534::J2534 &j2534, unsigned long baudrate, uint8_t cmId,
                   uint32_t startPos, uint32_t size, std::vector<uint8_t>& bin)
    : D2FlasherBase{ j2534, baudrate }
    , _cmId{ cmId }
    , _startPos{ startPos }
    , _size{ size }
    , _bin{ bin }
{}

D2Reader::~D2Reader() { }

void D2Reader::read(uint8_t cmId, unsigned long baudrate,
    uint32_t startPos, uint32_t size, std::vector<uint8_t>& bin) {
    const unsigned long protocolId = CAN;
    const unsigned long flags = CAN_29BIT_ID;

    openChannels(baudrate, true);

    setMaximumProgress(size);
    setCurrentProgress(0);

    runOnThread([this, cmId, &bin, protocolId, flags, startPos, size] {
        readFunction(cmId, bin, protocolId, flags, startPos, size);
        });
}

void D2Reader::startImpl() {
    const unsigned long protocolId = CAN;
    const unsigned long flags = CAN_29BIT_ID;

    readFunction(_cmId, _bin, protocolId, flags, _startPos, _size);
}

void D2Reader::readFunction(uint8_t cmId, std::vector<uint8_t>& bin,
    unsigned long protocolId, unsigned long flags, uint32_t startPos, uint32_t size)
{
    try {
        const auto ecuType = static_cast<common::ECUType>(cmId);
        canGoToSleep(protocolId, flags);
        std::this_thread::sleep_for(std::chrono::seconds(1));
        setCurrentState(FlasherState::StartBootloader);
        writeStartPrimaryBootloaderMsgAndCheckAnswer(ecuType, protocolId, flags);
        std::this_thread::sleep_for(std::chrono::seconds(1));
        setCurrentState(FlasherState::ReadFlash);
        for (uint32_t i = 0; i < size; ++i)
        {
            const auto currentPos = startPos + i;
            writeDataOffsetAndCheckAnswer(ecuType, currentPos, protocolId, flags);
            const auto checksumAnswer = writeMessagesAndReadMessage(*_channels[0], common::D2Messages::createCalculateChecksumMsg(
                ecuType, currentPos + 1).toPassThruMsgs(protocolId, flags));
            if (checksumAnswer.Data[5] == 0xB1)
            {
                bin.push_back(checksumAnswer.Data[6]);
            }
        }
        canWakeUp();
        setCurrentState(FlasherState::Done);
    }
    catch (std::exception& ex) {
        canWakeUp();
        setCurrentState(FlasherState::Error);
    }
}

} // namespace flasher
