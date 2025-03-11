#include "flasher/D2Reader.hpp"

#include <common/Util.hpp>
#include <common/D2Message.hpp>
#include <common/D2ProtocolCommonSteps.hpp>
#include <common/SBL.hpp>
#include <common/VBFParser.hpp>
#include <j2534/J2534.hpp>
#include <j2534/J2534Channel.hpp>

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
D2Reader::D2Reader(j2534::J2534 &j2534, FlasherParameters&& flasherParameters,
                   uint32_t startPos, uint32_t size, std::vector<uint8_t>& bin)
    : D2FlasherBase{ j2534, std::move(flasherParameters)}
    , _startPos{ startPos }
    , _size{ size }
    , _bin{ bin }
{}

D2Reader::~D2Reader() { }

size_t D2Reader::getMaximumFlashProgress() const
{
    return _size;
}

void D2Reader::eraseStep(j2534::J2534Channel&, uint8_t)
{
}

void D2Reader::writeStep(j2534::J2534Channel &channel, uint8_t ecuId)
{
    for (uint32_t i = 0; i < _size; ++i)
    {
        const auto currentPos = _startPos + i;
        common::D2ProtocolCommonSteps::jumpTo(channel, ecuId, currentPos);
        const auto checksumAnswer = writeMessagesAndReadMessage(channel, common::D2Messages::createCalculateChecksumMsg(
                                                                             ecuId, currentPos + 1));
        if (checksumAnswer.Data[5] == 0xB1)
        {
            _bin.push_back(checksumAnswer.Data[6]);
            incCurrentProgress(1);
        }
    }
}

} // namespace flasher
