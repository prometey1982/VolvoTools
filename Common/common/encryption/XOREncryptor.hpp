#pragma once

#include "EncryptorBase.hpp"

namespace common {

class XOREncryptor: public EncryptorBase {
public:
    XOREncryptor(std::map<std::string, std::string>&& params);

    virtual std::vector<uint8_t> encrypt(const std::vector<uint8_t>& data) override;
    virtual std::vector<uint8_t> decrypt(const std::vector<uint8_t>& data) override;

private:
    std::string _key;
};

} // namespace common
