#pragma once

#include <cinttypes>
#include <vector>

namespace common {

class RequestProcessorBase {
public:
    virtual ~RequestProcessorBase() = default;
    virtual std::vector<uint8_t> process(std::vector<uint8_t>&& service, std::vector<uint8_t>&& params = {}, size_t timeout = 1000) const = 0;
    virtual void disconnect() = 0;
    virtual bool connect() = 0;
};

} // namespace common
