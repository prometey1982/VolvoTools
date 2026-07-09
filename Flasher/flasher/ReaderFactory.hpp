#pragma once

#include "ReaderParametersProviderBase.hpp"

#include <j2534/J2534.hpp>

#include <memory>

namespace flasher {

class ReaderBase;

class ReaderFactory {
public:
    static std::unique_ptr<ReaderBase> create(
        j2534::J2534& j2534,
        const ReaderParametersProviderBase& params);

    static bool isD2Platform(common::CarPlatform p);
    static bool isUDSPlatform(common::CarPlatform p);
};

} // namespace flasher
