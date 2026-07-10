#include "flasher/D2FlasherBase.hpp"
#include "D2FlasherImpl.hpp"

#include "common/ICanChannel.hpp"
#include <common/Util.hpp>
#include <common/protocols/D2ProtocolCommonSteps.hpp>
#include <common/SBL.hpp>
#include <common/VBFParser.hpp>
#include <j2534/J2534.hpp>
#include <j2534/J2534Channel.hpp>

#define LOG_MODULE_NAME "flasher"
#include <common/LogHelper.hpp>

#include <ctime>
#include <numeric>

namespace flasher {

// D2FlasherBase methods

D2FlasherBase::D2FlasherBase(j2534::J2534 &j2534, common::CarPlatform carPlatform, uint32_t ecuId,
                              D2FlasherConfig&& config)
    : FlasherBase{ j2534, carPlatform, ecuId }
    , _config{ std::move(config) }
{
}

D2FlasherBase::~D2FlasherBase()
{
}

void D2FlasherBase::startImpl(std::vector<std::unique_ptr<common::ICanChannel>>& channels)
{
    std::this_thread::sleep_for(std::chrono::seconds(1));

    setCurrentProgress(0);
    D2FlasherImpl impl(channels, _carPlatform, static_cast<uint8_t>(_ecuId), _config.bootloader,
        [this](FlasherState state) {
            setCurrentState(state);
        },
        [this](size_t progress) {
            incCurrentProgress(progress);
        },
        [this](common::ICanChannel& ch, uint8_t id) {
            eraseStep(ch, id);
        },
        [this](common::ICanChannel& ch, uint8_t id) {
            writeStep(ch, id);
        });

    impl.setMaximumFlashProgressValue(getMaximumFlashProgress());
    setMaximumProgress(impl.getMaximumProgress());

    impl.run();
}

} // namespace flasher
