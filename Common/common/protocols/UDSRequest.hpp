#pragma once

#include "UDSMessage.hpp"

#include "j2534/J2534Channel.hpp"

#include <stdexcept>
#include <string>
#include <vector>

namespace common {

class UDSRequestTxError : public std::runtime_error {
public:
    UDSRequestTxError(unsigned long status, unsigned long written, const std::string& message);

    unsigned long status() const;
    unsigned long written() const;

private:
    unsigned long _status;
    unsigned long _written;
};

class UDSRequestRxTimeout : public std::runtime_error {
public:
    explicit UDSRequestRxTimeout(const std::string& message);
};

class UDSRequest {
public:
    UDSRequest(uint32_t canId, const std::vector<uint8_t>& data);
    UDSRequest(uint32_t canId, std::vector<uint8_t>&& data);

    std::vector<uint8_t> process(const j2534::J2534Channel& channel, size_t timeout = 1000);
    std::vector<uint8_t> process(const j2534::J2534Channel& channel, const std::vector<uint8_t>& checkData,
                                 size_t retryCount = 1, size_t timeout = 1000);

private:
    uint32_t _canId;
    uint8_t _requestId;
    std::vector<uint8_t> _data;
    UDSMessage _message;
};

} // namespace common
