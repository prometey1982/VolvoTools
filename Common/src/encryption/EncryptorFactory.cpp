#include "common/encryption/EncryptorFactory.hpp"

#include "common/encryption/XOREncryptor.hpp"

#include <easylogging++.h>

namespace common {

std::unique_ptr<EncryptorBase> EncryptorFactory::create(EncryptionType encryptionType, std::map<std::string, std::string>&& params)
{
    switch(encryptionType) {
    case EncryptionType::AES:
        LOG(WARNING) << "AES encryption non implemented";
        return {};
    case EncryptionType::XOR:
        return std::make_unique<XOREncryptor>(std::move(params));
    case EncryptionType::None:
        return {};
    }
    return {};
}

} // namespace common
