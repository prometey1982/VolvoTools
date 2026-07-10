#pragma once

#include "CanFrame.hpp"

#include <cstdint>
#include <vector>

namespace common {

class CanMessage {
public:
    static constexpr size_t CanPayloadSize = 8;
    using DataType = std::vector<uint8_t>;

    explicit CanMessage(uint32_t canId, const DataType& data);
    explicit CanMessage(uint32_t canId, DataType&& data);
    CanMessage(CanMessage&&) noexcept = default;
    CanMessage(const CanMessage&) noexcept = default;

    virtual std::vector<CanFrame> getFrames() const = 0;

protected:
    uint32_t getCanId() const { return _canId; }
    const DataType& getData() const { return _data; }

private:
    uint32_t _canId;
    const DataType _data;
};

} // namespace common
