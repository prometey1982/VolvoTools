#pragma once

#include <cstdint>
#include <memory>
#include <unordered_set>
#include <vector>

namespace flasher {

class PinCrackerStorage {
public:
    virtual ~PinCrackerStorage() = default;

    virtual bool isChecked(uint64_t pin) const = 0;
    virtual void markChecked(uint64_t pin) = 0;
    virtual void markRange(uint64_t from, uint64_t to) = 0;

    virtual void flush() {}
};

class NullPinCrackerStorage final : public PinCrackerStorage {
public:
    bool isChecked(uint64_t) const override { return false; }
    void markChecked(uint64_t) override {}
    void markRange(uint64_t, uint64_t) override {}
};

class InMemoryPinCrackerStorage final : public PinCrackerStorage {
public:
    bool isChecked(uint64_t pin) const override
    {
        return _checked.find(pin) != _checked.end();
    }

    void markChecked(uint64_t pin) override
    {
        _checked.insert(pin);
    }

    void markRange(uint64_t from, uint64_t to) override
    {
        for (uint64_t p = from; p <= to; ++p) {
            _checked.insert(p);
        }
    }

    const std::unordered_set<uint64_t>& checkedPins() const
    {
        return _checked;
    }

private:
    std::unordered_set<uint64_t> _checked;
};

} // namespace flasher
