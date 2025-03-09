#include "SBLProviderBase.hpp"

namespace flasher {

class SBLProviderVBF: public SBLProviderBase {
public:
    SBLProviderVBF(const common::VBF& sbl);

    virtual common::VBF getSBL(common::CarPlatform carPlatform, uint32_t ecuId, const std::string& additionalInfo) const override;
private:
    const common::VBF _sbl;
};

} // namespace flasher
