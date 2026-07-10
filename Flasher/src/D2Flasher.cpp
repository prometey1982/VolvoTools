#include "flasher/D2Flasher.hpp"

#include <common/protocols/D2ProtocolCommonSteps.hpp>

#include <common/SBL.hpp>
#include <common/VBFParser.hpp>
#include <j2534/J2534.hpp>
#include <j2534/J2534Channel.hpp>

#include <numeric>

namespace flasher {

D2Flasher::D2Flasher(j2534::J2534 &j2534, common::CarPlatform carPlatform, uint32_t ecuId,
                      D2FlasherConfig&& config)
    : D2FlasherBase{ j2534, carPlatform, ecuId, std::move(config) }
{
}

D2Flasher::~D2Flasher()
{
}

size_t D2Flasher::getMaximumFlashProgress() const
{
    return getProgressFromVBF(getConfig().flash);
}

bool D2Flasher::isBootloaderRequired() const
{
    return true;
}

void D2Flasher::eraseStep(common::ICanChannel &channel, uint8_t ecuId)
{
    common::D2ProtocolCommonSteps::eraseFlash(channel, ecuId, getConfig().flash);
}

void D2Flasher::writeStep(common::ICanChannel &channel, uint8_t ecuId)
{
    common::D2ProtocolCommonSteps::transferData(
        channel, ecuId, getConfig().flash,
        [this](size_t progress) {
        incCurrentProgress(progress);
    });
}

} // namespace flasher
