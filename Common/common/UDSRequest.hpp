#pragma once

#include "UDSMessage.hpp"

#include "j2534/J2534Channel.hpp"

#include <vector>

namespace common {

class UDSRequest {
public:
    UDSRequest(uint32_t canId, const std::vector<uint8_t>& data);
    UDSRequest(uint32_t canId, std::vector<uint8_t>&& data);

    std::vector<uint8_t> process(const j2534::J2534Channel& channel, size_t timeout = 1000);

private:
    uint8_t _requestId;
    UDSMessage _message;
};

} // namespace common
