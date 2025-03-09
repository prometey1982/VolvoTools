#pragma once

#include "CarPlatform.hpp"
#include "CMType.hpp"
#include "ConfigurationInfo.hpp"

#include <memory>
#include <vector>

namespace j2534 {
class J2534;
class J2534Channel;
} // namespace j2534

namespace common {

class J2534Info {
public:
    J2534Info(std::unique_ptr<j2534::J2534> j2534);
    ~J2534Info();

    void openChannels(CarPlatform carPlatform);
    void closeChannels();

    j2534::J2534& getJ2534() const;
    const std::vector<std::unique_ptr<j2534::J2534Channel>>& getChannels() const;
    j2534::J2534Channel& getChannelForEcu(uint32_t ecuId) const;
    j2534::J2534Channel& getChannelForCM(CMType cmType) const;

private:
    const std::vector<ConfigurationInfo> _configurationInfo;
    std::unique_ptr<j2534::J2534> _j2534;
    CarPlatform _carPlatform;
    std::vector<std::unique_ptr<j2534::J2534Channel>> _channels;
};

} // namespace common
