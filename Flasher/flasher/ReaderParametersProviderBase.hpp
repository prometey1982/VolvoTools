#pragma once

#include "ParamsTypes.hpp"
#include "SBLProviderBase.hpp"
#include "common/CarPlatform.hpp"

#include <cstdint>
#include <optional>
#include <string>

namespace flasher {

class ReaderParametersProviderBase {
public:
    ReaderParametersProviderBase(common::CarPlatform carPlatform,
                                 uint32_t ecuId,
                                 const std::string& cmInfo,
                                 std::unique_ptr<SBLProviderBase> sblProvider)
        : _carPlatform(carPlatform)
        , _ecuId(ecuId)
        , _cmInfo(cmInfo)
        , _sblProvider(std::move(sblProvider)){}

    virtual ~ReaderParametersProviderBase() = default;

    common::CarPlatform getCarPlatform() const { return _carPlatform; }
    uint32_t getEcuId() const { return _ecuId; }
    const std::string& getCmInfo() const { return _cmInfo; }

    virtual ReadRanges getReadRanges() const = 0;

    virtual std::optional<AuthorizationParams> getAuthParams() const { return std::nullopt; }
    virtual std::optional<BootloaderParams> getBootloaderParams() const
    {
        if(!_sblProvider) {
            return std::nullopt;
        }
        return BootloaderParams{ _sblProvider->getSBL(_carPlatform, _ecuId, _cmInfo) };
    }

protected:
    const common::CarPlatform _carPlatform;
    const uint32_t _ecuId;
    const std::string _cmInfo;
    std::unique_ptr<SBLProviderBase> _sblProvider;
};

} // namespace flasher
