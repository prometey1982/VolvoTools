#include "flasher/D2Flasher.hpp"

#include <common/Util.hpp>
//#include <common/protocols/D2Message.hpp>
#include <common/protocols/D2ProtocolCommonSteps.hpp>
#include <common/SBL.hpp>
#include <common/VBFParser.hpp>
#include <j2534/J2534.hpp>
#include <j2534/J2534Channel.hpp>

#include <numeric>
#include <time.h>

namespace flasher {

D2Flasher::D2Flasher(j2534::J2534 &j2534, FlasherParameters&& flasherParameters)
    : D2FlasherBase{ j2534, std::move(flasherParameters) }
{
}

D2Flasher::~D2Flasher()
{
}

size_t D2Flasher::getMaximumFlashProgress() const
{
    return getProgressFromVBF(getFlasherParameters().flash);
}

void D2Flasher::eraseStep(j2534::J2534Channel &channel, uint8_t ecuId)
{
    common::D2ProtocolCommonSteps::eraseFlash(channel, ecuId, getFlasherParameters().flash);
}

void D2Flasher::writeStep(j2534::J2534Channel &channel, uint8_t ecuId)
{
    common::D2ProtocolCommonSteps::transferData(
        channel, ecuId, getFlasherParameters().flash,
        [this](size_t progress) {
        incCurrentProgress(progress);
    });
}

} // namespace flasher
