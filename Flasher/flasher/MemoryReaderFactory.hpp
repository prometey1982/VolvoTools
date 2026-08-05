#pragma once

#include "ReaderParametersProviderBase.hpp"

#include <j2534/J2534.hpp>

#include <memory>

namespace flasher {

class ReaderBase;

class MemoryReaderFactory {
public:
    static std::unique_ptr<ReaderBase> create(
        j2534::J2534& j2534,
        const ReaderParametersProviderBase& params);

    static std::vector<uint32_t> getSupportedEcus(common::CarPlatform carPlatform);
};

} // namespace flasher
