#pragma once

#include "CarPlatform.hpp"

#include <vector>
#include <string>

namespace util {

class ChecksumHelper final {
public:
    ChecksumHelper(const common::CarPlatform carPlatform,
                   const uint8_t ecuId,
                   const std::string& additionalData);

    bool isSupported(const std::vector<uint8_t>& data) const;
    bool check(std::vector<uint8_t>& data) const;
    void update(std::vector<uint8_t>& data) const;

private:
    const common::CarPlatform _carPlatform;
    const uint8_t _ecuId;
    const std::string _additionalData;
};

}
