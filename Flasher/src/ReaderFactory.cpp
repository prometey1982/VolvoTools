#include "flasher/ReaderFactory.hpp"

#include "flasher/ReaderBase.hpp"
#include "flasher/D2ReaderAW55.hpp"
#include "flasher/D2ReaderChecksum.hpp"
#include "flasher/D2ReaderTF80.hpp"
#include "flasher/UDSReader.hpp"

#include <common/CarPlatform.hpp>

#include <stdexcept>

namespace flasher {

bool ReaderFactory::isD2Platform(common::CarPlatform p)
{
    return p == common::CarPlatform::P1 || p == common::CarPlatform::P1_UDS
        || p == common::CarPlatform::P2 || p == common::CarPlatform::P2_250
        || p == common::CarPlatform::P2_UDS || p == common::CarPlatform::P80;
}

bool ReaderFactory::isUDSPlatform(common::CarPlatform p)
{
    return p == common::CarPlatform::P3 || p == common::CarPlatform::Ford_UDS
        || p == common::CarPlatform::VAG || p == common::CarPlatform::Haval_UDS;
}

std::unique_ptr<ReaderBase> ReaderFactory::create(
    j2534::J2534& j2534,
    const ReaderParametersProviderBase& p)
{
    const auto platform = p.getCarPlatform();
    const auto ecuId = p.getEcuId();
    const auto& cmInfo = p.getCmInfo();
    const auto ranges = p.getReadRanges();

    // D2 ECM
    if (ecuId == 0x7A && isD2Platform(platform)) {
        return std::make_unique<D2ReaderChecksum>(j2534, platform, ecuId, ranges);
    }

    // D2 TCM
    if (ecuId == 0x6E && isD2Platform(platform)) {
        if (cmInfo == "aw55")
            return std::make_unique<D2ReaderAW55>(j2534, platform, ecuId, ranges);
        if (cmInfo == "tf80_p2")
            return std::make_unique<D2ReaderTF80>(j2534, platform, ecuId, ranges);
    }

    // UDS
    if (isUDSPlatform(platform)) {
        auto auth = p.getAuthParams();
        if (!auth)
            throw std::runtime_error("UDSReader requires PIN");
        return std::make_unique<UDSReader>(j2534, platform, ecuId, ranges, auth->pin);
    }

    throw std::runtime_error("Unsupported platform/ECU for reading");
}

} // namespace flasher
