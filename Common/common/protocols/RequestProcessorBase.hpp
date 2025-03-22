#pragma once

#include <cinttypes>
#include <vector>

namespace common {

class RequestProcessorBase {
public:
    virtual ~RequestProcessorBase() = default;
    virtual std::vector<uint8_t> process(std::vector<uint8_t>&& data, size_t timeout = 1000) const = 0;
};

} // namespace common
