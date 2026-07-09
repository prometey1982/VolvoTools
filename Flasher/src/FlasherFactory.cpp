#include "flasher/FlasherFactory.hpp"

#include "flasher/FlasherBase.hpp"
#include "flasher/D2Flasher.hpp"
#include "flasher/UDSFlasher.hpp"
#include "flasher/KWPFlasher.hpp"

#include <common/CarPlatform.hpp>

#include <stdexcept>

namespace flasher {

bool FlasherFactory::isD2Platform(common::CarPlatform p)
{
    return p == common::CarPlatform::P1 || p == common::CarPlatform::P1_UDS
        || p == common::CarPlatform::P2 || p == common::CarPlatform::P2_250
        || p == common::CarPlatform::P2_UDS || p == common::CarPlatform::P80;
}

bool FlasherFactory::isUDSPlatform(common::CarPlatform p)
{
    return p == common::CarPlatform::P3 || p == common::CarPlatform::Ford_UDS
        || p == common::CarPlatform::VAG || p == common::CarPlatform::Haval_UDS;
}

std::unique_ptr<FlasherBase> FlasherFactory::create(
    j2534::J2534& j2534,
    const FlasherParametersProviderBase& p)
{
    const auto platform = p.getCarPlatform();
    const auto ecuId = p.getEcuId();
    const auto& flash = p.getFlashData();

    // D2
    if (isD2Platform(platform)) {
        auto bootloader = p.getBootloaderParams();
        if (!bootloader)
            throw std::runtime_error("D2 flasher requires bootloader");
        return std::make_unique<D2Flasher>(j2534, platform, ecuId,
            D2FlasherConfig{ bootloader->bootloader, flash });
    }

    // UDS (Volvo, Ford)
    if (isUDSPlatform(platform) && platform != common::CarPlatform::VAG) {
        auto bootloader = p.getBootloaderParams();
        if (!bootloader)
            throw std::runtime_error("UDS flasher requires bootloader");
        auto auth = p.getAuthParams();
        std::array<uint8_t, 5> pinArray{};
        if (auth) {
            pinArray = {
                static_cast<uint8_t>((auth->pin >> 32) & 0xFF),
                static_cast<uint8_t>((auth->pin >> 24) & 0xFF),
                static_cast<uint8_t>((auth->pin >> 16) & 0xFF),
                static_cast<uint8_t>((auth->pin >> 8) & 0xFF),
                static_cast<uint8_t>(auth->pin & 0xFF)
            };
        }
        return std::make_unique<UDSFlasher>(j2534, platform, ecuId,
            UDSFlasherConfig{ pinArray, bootloader->bootloader, flash });
    }

    // VAG
    if (platform == common::CarPlatform::VAG) {
        auto auth = p.getAuthParams();
        auto enc = p.getEncryptionParams();
        std::array<uint8_t, 5> pinArray{};
        if (auth) {
            pinArray = {
                static_cast<uint8_t>((auth->pin >> 32) & 0xFF),
                static_cast<uint8_t>((auth->pin >> 24) & 0xFF),
                static_cast<uint8_t>((auth->pin >> 16) & 0xFF),
                static_cast<uint8_t>((auth->pin >> 8) & 0xFF),
                static_cast<uint8_t>(auth->pin & 0xFF)
            };
        }
        return std::make_unique<UDSFlasher>(j2534, platform, ecuId,
            UDSFlasherConfig{ pinArray, {}, flash });
    }

    throw std::runtime_error("Unsupported platform for flashing");
}

} // namespace flasher
