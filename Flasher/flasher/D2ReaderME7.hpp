#pragma once

#include "ReaderBase.hpp"

#include <memory>

namespace flasher {

class D2ReaderME7 : public ReaderBase {
public:
    D2ReaderME7(j2534::J2534& j2534, common::CarPlatform carPlatform, uint32_t ecuId,
                ReadRanges ranges, common::VBF bootloader);

protected:
    void startImpl(std::vector<std::unique_ptr<ICanChannel>>& channels) override;

private:
    common::VBF _bootloader;
};

} // namespace flasher
