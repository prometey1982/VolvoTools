#pragma once

#include "ReaderBase.hpp"

namespace flasher {

class D2ReaderAW55 : public ReaderBase {
public:
    D2ReaderAW55(j2534::J2534& j2534, common::CarPlatform carPlatform, uint32_t ecuId,
                 ReadRanges ranges);

protected:
    void startImpl(std::vector<std::unique_ptr<ICanChannel>>& channels) override;

};

} // namespace flasher
