#pragma once

#include <cstdint>
#include <vector>

class ICanChannel;

namespace common {

class UDSRequest {
public:
    UDSRequest(uint32_t canId, const std::vector<uint8_t>& data);
    UDSRequest(uint32_t canId, std::vector<uint8_t>&& data);

    std::vector<uint8_t> process(ICanChannel& channel, size_t timeout = 1000);
    std::vector<uint8_t> process(ICanChannel& channel, const std::vector<uint8_t>& checkData,
                                 size_t retryCount = 1, size_t timeout = 1000);

private:
    uint32_t _canId;
    uint8_t _requestId;
    std::vector<uint8_t> _data;
};

} // namespace common
