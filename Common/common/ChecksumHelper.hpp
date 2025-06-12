#pragma once

#include <vector>

namespace util {

class ChecksumHelper final {
public:
    bool isSupported(const std::vector<uint8_t>& data) const;
    bool check(std::vector<uint8_t>& data) const;
    void update(std::vector<uint8_t>& data) const;
};

}
