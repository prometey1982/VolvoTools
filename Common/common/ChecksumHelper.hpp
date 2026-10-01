#pragma once

#include <vector>

namespace util {

class ChecksumHelper final {
public:
    bool isSupported(const std::vector<uint8_t>& data) const;
    bool checkME(std::vector<uint8_t>& data) const;
    void updateME(std::vector<uint8_t>& data) const;
};

}
