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
        const j2534::BaseMessage& msgs) {
        channel.clearRx();
        unsigned long msgsNum = 1;
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
D2Reader::D2Reader(common::J2534Info &j2534Info, FlasherParameters&& flasherParameters,
                   uint32_t startPos, uint32_t size, std::vector<uint8_t>& bin)
    : D2FlasherBase{ j2534Info, std::move(flasherParameters)}
    , _startPos{ startPos }
    , _size{ size }
    , _bin{ bin }
{}

D2Reader::~D2Reader() { }

void D2Reader::read(uint32_t startPos, uint32_t size, std::vector<uint8_t>& bin) {
    setMaximumProgress(size);
    setCurrentProgress(0);

    runOnThread([this, &bin, startPos, size] {
        readFunction(bin, startPos, size);
        });
}

void D2Reader::startImpl() {
    readFunction(_bin, _startPos, _size);
}

void D2Reader::readFunction(std::vector<uint8_t>& bin, uint32_t startPos, uint32_t size)
{
    try {
        const auto ecuId{ getFlasherParameters().ecuId };
        canGoToSleep();
        std::this_thread::sleep_for(std::chrono::seconds(1));
        setCurrentState(FlasherState::StartBootloader);
        writeStartPrimaryBootloaderMsgAndCheckAnswer(ecuId);
        std::this_thread::sleep_for(std::chrono::seconds(1));
        setCurrentState(FlasherState::ReadFlash);
        auto& channel{ getJ2534Info().getChannelForEcu(ecuId) };
        for (uint32_t i = 0; i < size; ++i)
        {
            const auto currentPos = startPos + i;
            writeDataOffsetAndCheckAnswer(ecuId, currentPos);
            const auto checksumAnswer = writeMessagesAndReadMessage(channel, common::D2Messages::createCalculateChecksumMsg(
                ecuId, currentPos + 1));
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
