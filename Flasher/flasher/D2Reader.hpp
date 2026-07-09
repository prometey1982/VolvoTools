#pragma once

#include "ReaderBase.hpp"

#include "SBLProviderBase.hpp"

#include <memory>

namespace flasher {

class D2Reader : public ReaderBase {
public:
    D2Reader(j2534::J2534& j2534, common::CarPlatform carPlatform, uint32_t ecuId,
             ReadRanges ranges, std::shared_ptr<SBLProviderBase> sblProvider);

protected:
    void startImpl(std::vector<std::unique_ptr<ICanChannel>>& channels) override;

private:
    std::shared_ptr<SBLProviderBase> _sblProvider;
};

} // namespace flasher
