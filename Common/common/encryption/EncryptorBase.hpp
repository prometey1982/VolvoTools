#pragma once

#include <vector>
#include <map>
#include <string>

namespace common {

class EncryptorBase {
public:
    EncryptorBase(std::map<std::string, std::string>&& params);
    virtual ~EncryptorBase();

    virtual std::vector<uint8_t> encrypt(const std::vector<uint8_t>& data) = 0;
    virtual std::vector<uint8_t> decrypt(const std::vector<uint8_t>& data) = 0;

protected:
    const std::map<std::string, std::string> _params;
};

} // namespace common
