#pragma once

#include "ReaderBase.hpp"

namespace flasher {

class D2ReaderME7Memory : public ReaderBase {
public:
    D2ReaderME7Memory(j2534::J2534& j2534, common::CarPlatform carPlatform, uint32_t ecuId,
                 ReadRanges ranges);

protected:
    void startImpl(const std::vector<std::unique_ptr<common::ICanChannel>>& channels) override;

};

} // namespace flasher
