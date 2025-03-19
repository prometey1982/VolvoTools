#pragma once

#include "TP20Message.hpp"

#include "j2534/J2534Channel.hpp"

#include <vector>

namespace common {

    class TP20Request {
    public:
        TP20Request(uint32_t canId, uint32_t responseCanId, const std::vector<uint8_t>& data);
        TP20Request(uint32_t canId, uint32_t responseCanId, std::vector<uint8_t>&& data);

        std::vector<uint8_t> process(const j2534::J2534Channel& channel, size_t timeout = 1000) const;

    private:
        const uint8_t _requestId;
        const uint32_t _responseCanId;
        TP20Message _message;
    };

} // namespace common
