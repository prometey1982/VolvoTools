#pragma once

#include "ReaderBase.hpp"

#include <common/CanIdProvider.hpp>

namespace flasher {

class UDSReaderMemory : public ReaderBase {
public:
    UDSReaderMemory(j2534::J2534& j2534, common::CarPlatform carPlatform, uint32_t ecuId,
              ReadRanges ranges);

protected:
    void startImpl(const std::vector<std::unique_ptr<common::ICanChannel>>& channels) override;

private:
    std::unique_ptr<common::CanIdProvider> _canIdProvider;
};

} // namespace flasher
