#pragma once

#include "ReaderBase.hpp"

namespace flasher {

class UDSReader : public ReaderBase {
public:
    UDSReader(j2534::J2534& j2534, common::CarPlatform carPlatform, uint32_t ecuId,
              ReadRanges ranges, uint64_t pin);

protected:
    void startImpl(std::vector<std::unique_ptr<common::ICanChannel>>& channels) override;

private:
    uint64_t _pin;
    uint32_t _canId;
};

} // namespace flasher
