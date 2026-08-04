#include "flasher/MemoryReaderFactory.hpp"

#include "flasher/ReaderBase.hpp"
#include "flasher/D2ReaderAW55.hpp"
#include "flasher/D2ReaderME7Memory.hpp"
#include "flasher/D2ReaderTF80.hpp"
#include "flasher/UDSReaderMemory.hpp"

#include <common/protocols/D2ECUType.hpp>
#include <common/CarPlatform.hpp>
#include <common/utility.hpp>

#include <stdexcept>

namespace flasher {

bool MemoryReaderFactory::isD2Platform(common::CarPlatform p)
{
    return p == common::CarPlatform::P1 || p == common::CarPlatform::P1_UDS
        || p == common::CarPlatform::P2 || p == common::CarPlatform::P2_250
        || p == common::CarPlatform::P2_UDS || p == common::CarPlatform::P80;
}

bool MemoryReaderFactory::isUDSPlatform(common::CarPlatform p)
{
    return p == common::CarPlatform::P3 || p == common::CarPlatform::Ford_UDS
        || p == common::CarPlatform::VAG || p == common::CarPlatform::Haval_UDS;
}

std::unique_ptr<ReaderBase> MemoryReaderFactory::create(
    j2534::J2534& j2534,
    const ReaderParametersProviderBase& p)
{
    const auto platform = p.getCarPlatform();
    const auto ecuId = p.getEcuId();
    const auto& cmInfo = p.getCmInfo();
    const auto ranges = p.getReadRanges();

    if(isD2Platform(platform)) {
        if (ecuId == to_underlying(common::D2ECUType::ECM_ME)) {
            return std::make_unique<D2ReaderME7Memory>(j2534, platform, ecuId, ranges);
        }
        else if (ecuId == to_underlying(common::D2ECUType::TCM)) {
            if (cmInfo == "aw55_p2")
                return std::make_unique<D2ReaderAW55>(j2534, platform, ecuId, ranges);
            if (cmInfo == "tf80_p2")
                return std::make_unique<D2ReaderTF80>(j2534, platform, ecuId, ranges);
        }
    }

    // UDS
    if (isUDSPlatform(platform)) {
        return std::make_unique<UDSReaderMemory>(j2534, platform, ecuId, ranges);
    }

    throw std::runtime_error("Unsupported platform/ECU for reading");
}

} // namespace flasher
