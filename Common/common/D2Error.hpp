#pragma once

#include <stdexcept>


namespace common {

class D2Error: public std::runtime_error {
public:
    D2Error(uint8_t errorCode);

    uint8_t getErrorCode() const noexcept;

private:
    uint8_t _errorCode;
};

} // namespace common
