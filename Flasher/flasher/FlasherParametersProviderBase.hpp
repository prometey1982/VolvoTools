#pragma once

#include "ParamsTypes.hpp"
#include "common/CarPlatform.hpp"

#include <cstdint>
#include <optional>
#include <string>

namespace flasher {

class FlasherParametersProviderBase {
public:
    FlasherParametersProviderBase(common::CarPlatform carPlatform, uint32_t ecuId, const std::string& cmInfo)
        : _carPlatform(carPlatform)
        , _ecuId(ecuId)
        , _cmInfo(cmInfo) {}

    virtual ~FlasherParametersProviderBase() = default;

    common::CarPlatform getCarPlatform() const { return _carPlatform; }
    uint32_t getEcuId() const { return _ecuId; }
    const std::string& getCmInfo() const { return _cmInfo; }

    virtual const common::VBF& getFlashData() const = 0;

    virtual std::optional<AuthorizationParams> getAuthParams() const { return std::nullopt; }
    virtual std::optional<BootloaderParams> getBootloaderParams() const { return std::nullopt; }
    virtual std::optional<EncryptionParams> getEncryptionParams() const { return std::nullopt; }

private:
    const common::CarPlatform _carPlatform;
    const uint32_t _ecuId;
    const std::string _cmInfo;
};

} // namespace flasher
