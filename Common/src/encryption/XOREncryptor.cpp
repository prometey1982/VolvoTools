#include "common/encryption/XOREncryptor.hpp"

#include <stdexcept>

namespace common {

namespace {

std::string getKey(const std::map<std::string, std::string>& params)
{
    const auto it = params.find("key");
    if(it != params.cend()) {
        return it->second;
    }
    throw std::runtime_error("Param 'key' required for XOR encryption doesn't found");
}

} // namespace

XOREncryptor::XOREncryptor(std::map<std::string, std::string>&& params)
    : EncryptorBase{ std::move(params) }
    , _key{ getKey(_params) }
{
}

std::vector<uint8_t> XOREncryptor::encrypt(const std::vector<uint8_t>& data)
{
    std::vector<uint8_t> result(data.size());
    const size_t keySize{ _key.size() };
    for(size_t i = 0; i < data.size(); ++i) {
        result[i] = _key[i % keySize] ^ data[i];
    }
    return result;
}

std::vector<uint8_t> XOREncryptor::decrypt(const std::vector<uint8_t>& data)
{
    return encrypt(data);
}

} // namespace common
