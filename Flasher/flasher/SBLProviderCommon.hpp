#include "SBLProviderBase.hpp"

namespace flasher {

class SBLProviderCommon: public SBLProviderBase {
public:
    virtual common::VBF getSBL(common::CarPlatform carPlatform, uint32_t ecuId, const std::string& additionalInfo) const override;
};

}
