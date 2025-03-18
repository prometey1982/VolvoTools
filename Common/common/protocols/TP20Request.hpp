#pragma once

#include "UDSMessage.hpp"

#include "j2534/J2534Channel.hpp"

#include <vector>

namespace common {

    class KWPRequest {
    public:
        KWPRequest(uint32_t canId, uint32_t responseCanId, const std::vector<uint8_t>& data);
        KWPRequest(uint32_t canId, uint32_t responseCanId, std::vector<uint8_t>&& data);

        std::vector<uint8_t> process(const j2534::J2534Channel& channel, size_t timeout = 1000) const;

    private:
        const uint8_t _requestId;
        const uint32_t _responseCanId;
        UDSMessage _message;
    };

} // namespace common
