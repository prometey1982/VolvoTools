#include "common/CanIdProvider.hpp"
#include "common/Util.hpp"

#include <stdexcept>

namespace common {

std::unique_ptr<CanIdProvider> createCanIdProvider(
    ProtocolType protocol,
    uint32_t canIdBitSize,
    uint32_t ecuId,
    uint32_t canId,
    uint32_t funcGroup)
{
    if (protocol == ProtocolType::ISO15765 && canIdBitSize == 11) {
        return std::make_unique<CanId11bit>(canId);
    }

    if (protocol == ProtocolType::ISO15765 && canIdBitSize == 29) {
        return std::make_unique<CanId29bit>(ecuId, funcGroup);
    }

    if (protocol == ProtocolType::CAN && canIdBitSize == 29) {
        return std::make_unique<CanIdD2>();
    }

    if (protocol == ProtocolType::ISO14230 || protocol == ProtocolType::ISO9141) {
        return std::make_unique<CanIdTP20>();
    }

    throw std::runtime_error("Unsupported protocol for CanIdProvider");
}

std::unique_ptr<CanIdProvider> createCanIdProviderForEcu(CarPlatform carPlatform, uint32_t ecuId)
{
    const auto [busInfo, ecuInfo] = getEcuInfoByEcuId(carPlatform, ecuId);
    return createCanIdProvider(
        busInfo.protocol,
        busInfo.canIdBitSize,
        ecuInfo.ecuId,
        ecuInfo.canId,
        0x33);
}

} // namespace common
