#pragma once

#include <cstdint>
#include <vector>

namespace common {
    class ICanChannel;

    class TP20Request {
    public:
        TP20Request(uint32_t canId, uint32_t responseCanId, const std::vector<uint8_t>& data);
        TP20Request(uint32_t canId, uint32_t responseCanId, std::vector<uint8_t>&& data);

        std::vector<uint8_t> process(ICanChannel& channel, size_t timeout = 1000) const;

    private:
        const uint32_t _canId;
        const uint8_t _requestId;
        const uint32_t _responseCanId;
        std::vector<uint8_t> _data;
    };

} // namespace common
