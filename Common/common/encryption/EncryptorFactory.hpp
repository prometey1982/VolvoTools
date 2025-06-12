#pragma once

#include "EncryptionType.hpp"

#include <map>
#include <memory>
#include <string>

namespace common {

class EncryptorBase;

class EncryptorFactory {
public:
    static std::unique_ptr<EncryptorBase> create(EncryptionType encryptionType, std::map<std::string, std::string>&& params);
};

} // namespace common
