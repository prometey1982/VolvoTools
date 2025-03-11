#pragma once

#include "D2Message.hpp"

#include "j2534/J2534Channel.hpp"

#include <vector>

namespace common {

class D2Request {
public:
    explicit D2Request(uint8_t ecuId, const std::vector<uint8_t>& data);
    explicit D2Request(uint8_t ecuId, std::vector<uint8_t>&& data);
    explicit D2Request(D2Message&& message);
    explicit D2Request(const D2Message& message);

    std::vector<uint8_t> process(const j2534::J2534Channel& channel, size_t timeout = 1000);

private:
    D2Message _message;
};

} // namespace common
