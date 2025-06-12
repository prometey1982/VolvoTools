#include "flasher/SBLProviderVBF.hpp"

namespace flasher {

SBLProviderVBF::SBLProviderVBF(const common::VBF& sbl)
    : _sbl{ sbl }
{
}

common::VBF SBLProviderVBF::getSBL(common::CarPlatform, uint32_t, const std::string&) const
{
    return _sbl;
}

} // namespace flasher
