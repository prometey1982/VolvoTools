#include "LogParameter.hpp"

namespace {
double processSign(uint32_t value, bool isSigned, size_t size) {
  if (isSigned) {
    switch (size) {
    case 1:
      return static_cast<int8_t>(value);
    case 2:
      return static_cast<int16_t>(value);
    default:
      return static_cast<int32_t>(value);
    }
  } else {
    return value;
  }
}
} // namespace

namespace logger {
LogParameter::LogParameter(const std::string &name, uint32_t addr, size_t size,
                           uint32_t bitmask, const std::string &unit,
                           bool isSigned, bool isInverseConversion,
                           double factor, double offset,
                           const std::string &description)
    : _name{name}, _addr{addr}, _size{size}, _bitmask{bitmask}, _unit{unit},
      _isSigned{isSigned}, _isInverseConversion{isInverseConversion},
      _factor{factor}, _offset{offset}, _description{description} {}

const std::string &LogParameter::name() const { return _name; }

uint32_t LogParameter::addr() const { return _addr; }

size_t LogParameter::size() const { return _size; }

uint32_t LogParameter::bitmask() const { return _bitmask; }

const std::string &LogParameter::unit() const { return _unit; }

const bool LogParameter::isSigned() const { return _isSigned; }

const bool LogParameter::isInverseConversion() const {
  return _isInverseConversion;
}

double LogParameter::factor() const { return _factor; }

double LogParameter::offset() const { return _offset; }

const std::string &LogParameter::description() const { return _description; }

double LogParameter::formatValue(uint32_t value) const {
  if (_bitmask) {
    value &= _bitmask;
  }
  if (_isInverseConversion) {
    return _factor / (processSign(value, _isSigned, _size) + _offset);
  } else {
    return processSign(value, _isSigned, _size) * _factor + _offset;
  }
}

} // namespace logger
