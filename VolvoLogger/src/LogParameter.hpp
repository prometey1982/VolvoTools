#pragma once

#include <cstdint>
#include <string>

namespace logger {
class LogParameter final {
public:
  LogParameter() = default;
  LogParameter(const std::string &name, uint32_t addr, size_t size,
               uint32_t bitmask, const std::string &unit, bool isSigned,
               bool isInverseConversion, double factor, double offset,
               const std::string &description);
  LogParameter(const LogParameter &rhs) = default;

  const std::string &name() const;
  uint32_t addr() const;
  size_t size() const;
  uint32_t bitmask() const;
  const std::string &unit() const;
  const bool isSigned() const;
  const bool isInverseConversion() const;
  double factor() const;
  double offset() const;
  const std::string &description() const;
  double formatValue(uint32_t value) const;

private:
  std::string _name;
  uint32_t _addr;
  size_t _size;
  uint32_t _bitmask;
  std::string _unit;
  bool _isSigned;
  bool _isInverseConversion;
  double _factor;
  double _offset;
  std::string _description;
};

} // namespace logger
