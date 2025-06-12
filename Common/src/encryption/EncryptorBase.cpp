#include "common/encryption/EncryptorBase.hpp"

namespace common {

EncryptorBase::EncryptorBase(std::map<std::string, std::string>&& params)
    : _params{ std::move(params) }
{
}

EncryptorBase::~EncryptorBase() = default;

} // namespace common
