#pragma once

#include "ParamsTypes.hpp"
#include "SBLProviderBase.hpp"
#include "common/CarPlatform.hpp"

#include <cstdint>
#include <optional>
#include <string>

namespace flasher {

class FlasherParametersProviderBase {
public:
    FlasherParametersProviderBase(common::CarPlatform carPlatform, uint32_t ecuId,
                                  const std::string& cmInfo,
                                  const common::VBF& flash,
                                  std::unique_ptr<SBLProviderBase> sblProvider)
        : _carPlatform(carPlatform)
        , _ecuId(ecuId)
        , _cmInfo(cmInfo)
        , _flash(flash)
        , _sblProvider(std::move(sblProvider)){}

    virtual ~FlasherParametersProviderBase() = default;

    common::CarPlatform getCarPlatform() const { return _carPlatform; }
    uint32_t getEcuId() const { return _ecuId; }
    const std::string& getCmInfo() const { return _cmInfo; }

    virtual const common::VBF& getFlashData() const { return _flash; }

    virtual std::optional<AuthorizationParams> getAuthParams() const { return std::nullopt; }
    virtual std::optional<BootloaderParams> getBootloaderParams() const
    {
        if(!_sblProvider) {
            return std::nullopt;
        }
        return BootloaderParams{ _sblProvider->getSBL(_carPlatform, _ecuId, _cmInfo) };
    }
    virtual std::optional<EncryptionParams> getEncryptionParams() const { return std::nullopt; }

private:
    const common::CarPlatform _carPlatform;
    const uint32_t _ecuId;
    const std::string _cmInfo;
    const common::VBF _flash;
    std::unique_ptr<SBLProviderBase> _sblProvider;
};

} // namespace flasher
