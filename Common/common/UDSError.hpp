#pragma once

#include <stdexcept>


namespace common {

class UDSError: public std::runtime_error {
public:
    UDSError(uint8_t errorCode);

    uint8_t getErrorCode() const noexcept;

private:
    uint8_t _errorCode;
};

} // namespace common
