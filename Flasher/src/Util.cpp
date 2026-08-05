#include "Util.hpp"

namespace flasher {

bool isD2Platform(common::CarPlatform p)
{
    return p == common::CarPlatform::P1 || p == common::CarPlatform::P1_UDS
        || p == common::CarPlatform::P2 || p == common::CarPlatform::P2_250
        || p == common::CarPlatform::P2_UDS || p == common::CarPlatform::P80;
}

bool isUDSPlatform(common::CarPlatform p)
{
    return p == common::CarPlatform::P3 || p == common::CarPlatform::Ford_UDS
        || p == common::CarPlatform::VAG || p == common::CarPlatform::Haval_UDS;
}

} // namespace flasher
