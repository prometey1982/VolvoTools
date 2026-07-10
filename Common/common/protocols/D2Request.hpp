#pragma once

#include "D2Message.hpp"

#include <cstdint>
#include <vector>


namespace common {
class ICanChannel;

class D2Request {
public:
    explicit D2Request(uint8_t ecuId, const std::vector<uint8_t>& data);
    explicit D2Request(uint8_t ecuId, std::vector<uint8_t>&& data);
    explicit D2Request(D2Message&& message);
    explicit D2Request(const D2Message& message);

    std::vector<uint8_t> process(ICanChannel& channel, size_t timeout = 1000, size_t sendMessageDelay = 0);

private:
    D2Message _message;
};

} // namespace common