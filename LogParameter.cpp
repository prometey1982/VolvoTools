#include "LogParameter.hpp"

namespace logger
{
	LogParameter::LogParameter(const std::string& name, uint32_t addr, size_t size, uint32_t bitmask,
		const std::string& unit, bool isSigned, bool isInverseConversion, double factor, double offset,
		const std::string& description)
		: _name{name}
		, _addr{addr}
		, _size{size}
		, _bitmask{bitmask}
		, _unit{unit}
		, _isSigned{isSigned}
		, _isInverseConversion{isInverseConversion}
		, _factor{factor}
		, _offset{offset}
		, _description{description}
	{
	}

	const std::string& LogParameter::name() const
	{
		return _name;
	}

	uint32_t LogParameter::addr() const
	{
		return _addr;
	}

	size_t LogParameter::size() const
	{
		return _size;
	}

	uint32_t LogParameter::bitmask() const
	{
		return _bitmask;
	}

	const std::string& LogParameter::unit() const
	{
		return _unit;
	}

	const bool LogParameter::isSigned() const
	{
		return _isSigned;
	}

	const bool LogParameter::isInverseConversion() const
	{
		return _isInverseConversion;
	}

	double LogParameter::factor() const
	{
		return _factor;
	}

	double LogParameter::offset() const
	{
		return _offset;
	}

	const std::string& LogParameter::description() const
	{
		return _description;
	}

	double LogParameter::formatValue(uint32_t value) const
	{
		if (_isInverseConversion) {
			return _factor / (value - _offset);
		}
		else {
			return value * _factor - _offset;
		}
	}

} // namespace logger
