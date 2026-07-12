#pragma once

#include "D2FlasherBase.hpp"

#include <common/GenericProcess.hpp>
#include <common/VBF.hpp>

#include <vector>

namespace j2534 {
class J2534;
} // namespace j2534

namespace common {
class ICanChannel;
} // namespace
namespace flasher {

class D2Flasher: public D2FlasherBase {
public:
    D2Flasher(j2534::J2534 &j2534, common::CarPlatform carPlatform, uint32_t ecuId,
              D2FlasherConfig&& config);
    ~D2Flasher();

private:
    size_t getMaximumFlashProgress() const override;
    bool isBootloaderRequired() const override;
    void eraseStep(common::ICanChannel &channel, uint8_t ecuId) override;
    void writeStep(common::ICanChannel &channel, uint8_t ecuId) override;
};

} // namespace flasher
