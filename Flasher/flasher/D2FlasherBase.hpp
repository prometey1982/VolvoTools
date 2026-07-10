#pragma once

#include "FlasherBase.hpp"
#include "FlasherConfigs.hpp"

#include <common/GenericProcess.hpp>
#include <common/CMType.hpp>
#include <common/VBF.hpp>

namespace common {
class ICanChannel;
} // namespace common

namespace flasher {

class D2FlasherBase: public FlasherBase {
public:
    D2FlasherBase(j2534::J2534 &j2534, common::CarPlatform carPlatform, uint32_t ecuId,
                  D2FlasherConfig&& config);
    ~D2FlasherBase();

protected:
    void startImpl(std::vector<std::unique_ptr<common::ICanChannel>>& channels) override final;

    virtual size_t getMaximumFlashProgress() const = 0;
    virtual bool isBootloaderRequired() const = 0;
    virtual void eraseStep(common::ICanChannel &channel, uint8_t ecuId) = 0;
    virtual void writeStep(common::ICanChannel &channel, uint8_t ecuId) = 0;

    const D2FlasherConfig& getConfig() const { return _config; }

private:
    D2FlasherConfig _config;
};

} // namespace flasher
