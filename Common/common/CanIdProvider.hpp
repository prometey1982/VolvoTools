#pragma once

#include "CarPlatform.hpp"

#include <cstdint>
#include <memory>

namespace common {

class CanIdProvider {
public:
    virtual ~CanIdProvider() = default;

    virtual uint32_t getPhysCanId() const = 0;
    virtual uint32_t getFuncCanId() const = 0;
    virtual bool isExtendedId() const = 0;
};

class CanId11bit final : public CanIdProvider {
public:
    explicit CanId11bit(uint32_t canId)
        : _physCanId{ canId }
    {}

    uint32_t getPhysCanId() const override { return _physCanId; }
    uint32_t getFuncCanId() const override { return 0x7DF; }
    bool isExtendedId() const override { return false; }

private:
    uint32_t _physCanId;
};

class CanId29bit final : public CanIdProvider {
public:
    CanId29bit(uint32_t ps, uint32_t funcGroup, uint32_t sa = 0xF1)
        : _physCanId{ buildPhysCanId(ps, sa) }
        , _funcCanId{ buildFuncCanId(funcGroup, sa) }
    {}

    uint32_t getPhysCanId() const override { return _physCanId; }
    uint32_t getFuncCanId() const override { return _funcCanId; }
    bool isExtendedId() const override { return true; }

private:
    static uint32_t buildPhysCanId(uint32_t ps, uint32_t sa)
    {
        return 0x18DA00F1 | ((ps & 0xFF) << 8);
    }

    static uint32_t buildFuncCanId(uint32_t group, uint32_t sa)
    {
        return 0x18DB00F1 | ((group & 0xFF) << 8);
    }

    uint32_t _physCanId;
    uint32_t _funcCanId;
};

class CanIdD2 final : public CanIdProvider {
public:
    static constexpr uint32_t D2_CAN_ID = 0xFFFFE;

    uint32_t getPhysCanId() const override { return D2_CAN_ID; }
    uint32_t getFuncCanId() const override { return D2_CAN_ID; }
    bool isExtendedId() const override { return true; }
};

class CanIdTP20 final : public CanIdProvider {
public:
    uint32_t getPhysCanId() const override { return 0; }
    uint32_t getFuncCanId() const override { return 0; }
    bool isExtendedId() const override { return false; }
};

std::unique_ptr<CanIdProvider> createCanIdProvider(
    unsigned long protocolId,
    uint32_t canIdBitSize,
    uint32_t ecuId,
    uint32_t canId,
    uint32_t funcGroup = 0x33);

// Создаёт CanIdProvider для ЭБУ по его CarPlatform + ecuId (Address из YAML).
// Автоматически определяет протокол из конфигурации.
std::unique_ptr<CanIdProvider> createCanIdProviderForEcu(CarPlatform carPlatform, uint32_t ecuId);

} // namespace common
