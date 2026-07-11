#include "flasher/SBLProviderCommon.hpp"

#include "common/protocols/D2ECUType.hpp"
#include "common/VBFParser.hpp"
#include "common/SBL.hpp"
#include "common/Util.hpp"

namespace flasher {

common::VBF SBLProviderCommon::getSBL(common::CarPlatform carPlatform, uint32_t ecuId,
                                      const std::string& additionalInfo) const
{
    common::VBFParser parser;
    switch(carPlatform) {
    case common::CarPlatform::P1:
    case common::CarPlatform::P2:
    case common::CarPlatform::P2_250:
        if(ecuId == common::to_underlying(common::D2ECUType::ECM_ME)) {
            if(additionalInfo == "me9_p1") {
                return parser.parse(common::SBLData::P1_ME9_SBL);
            }
            else {
                return parser.parse(common::SBLData::P2_ME7_SBL);
            }
        }
        break;
    case common::CarPlatform::P3:
        if(ecuId == 0x10) {
            if(additionalInfo == "me9_p3") {
                return parser.parse(common::SBLData::P3_ME9_SBL);
            }
            else if(additionalInfo == "denso_p3") {
                return parser.parse(common::SBLData::P3_3_2_SBL);
            }
        }
    case common::CarPlatform::Ford_UDS:
        if(ecuId == 0x10) {
            if(additionalInfo == "me9_p3") {
                return parser.parse(common::SBLData::P3_ME9_SBL);
            }
        }
    default:
        break;
    }
    return {{}, {}};
}

} // namespace flasher
