#pragma once

#include "ReaderBase.hpp"

#include <memory>

namespace flasher {

class D2ReaderDEMGen2 : public ReaderBase {
public:
    D2ReaderDEMGen2(j2534::J2534& j2534, common::CarPlatform carPlatform, uint32_t ecuId,
                    ReadRanges ranges, common::VBF bootloader);

protected:
    void startImpl(const std::vector<std::unique_ptr<common::ICanChannel>>& channels) override;

private:
    void readStep(common::ICanChannel &channel, uint8_t ecuId);

private:
    common::VBF _bootloader;
};

} // namespace flasher
