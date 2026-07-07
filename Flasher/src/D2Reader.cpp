#include "flasher/D2Reader.hpp"

#include <common/ICanChannel.hpp>
#include <common/Util.hpp>
#include <common/protocols/D2ProtocolCommonSteps.hpp>
#include <common/SBL.hpp>
#include <common/VBFParser.hpp>
#include <j2534/J2534.hpp>
#include <j2534/J2534Channel.hpp>

namespace {

    constexpr uint32_t D2_CAN_ID = 0xFFFFE;

    CanFrame writeMessagesAndReadMessage(ICanChannel& channel,
                                          const CanFrame& msg) {
        channel.clearRx();
        if (!channel.send(msg)) {
            throw std::runtime_error("write msgs error");
        }
        CanFrame response;
        if (!channel.receive(response, 3000)) {
            throw std::runtime_error("Failed to receive message");
        }
        return response;
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

bool D2Reader::isBootloaderRequired() const
{
    return false;
}

void D2Reader::eraseStep(ICanChannel&, uint8_t)
{
}

void D2Reader::writeStep(ICanChannel &channel, uint8_t ecuId)
{
    for (uint32_t i = 0; i < _size; ++i)
    {
        const auto currentPos = _startPos + i;
        common::D2ProtocolCommonSteps::jumpTo(channel, ecuId, currentPos);
        auto calcMsg = common::toVector(currentPos + 1);
        std::vector<uint8_t> payload(8, 0);
        payload[0] = ecuId;
        payload[1] = 0xB4;
        payload[2] = 0x15;
        payload[3] = 0x22;
        payload[4] = calcMsg[0];
        payload[5] = calcMsg[1];
        payload[6] = calcMsg[2];
        payload[7] = calcMsg[3];
        const auto checksumAnswer = writeMessagesAndReadMessage(channel,
            {D2_CAN_ID, std::move(payload), true});
        if (checksumAnswer.data.size() >= 2 && checksumAnswer.data[1] == 0xB1)
        {
            _bin.push_back(checksumAnswer.data[2]);
            incCurrentProgress(1);
        }
    }
}

} // namespace flasher
