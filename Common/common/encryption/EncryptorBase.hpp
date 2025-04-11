#pragma once

#include <vector>

namespace common {

    class EncryptorBase {
    public:
        virtual ~EncryptorBase() = default;

        virtual std::vector<uint8_t> encrypt(const std::vector<uint8_t>& data) = 0;
    };

} // namespace common
