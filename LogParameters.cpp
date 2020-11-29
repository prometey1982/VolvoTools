#include "LogParameters.hpp"

#pragma warning(push)
#pragma warning(disable: 4996)
#include "fast-cpp-csv-parser/csv.h"
#pragma warning(pop)

namespace logger
{

	LogParameters::LogParameters(const std::string& path)
	{
		io::CSVReader<10> reader{ path };
		std::string name;
		std::string addr;
		size_t size;
		std::string bitmask;
		std::string unit;
		uint8_t isSigned;
		uint8_t isInverseConversion;
		double factor;
		double offset;
		std::string description;
		reader.read_header(io::ignore_extra_column, "Name", "Address", "Size", "Bitmask", "Unit", "S", "I", "A", "B", "Comment");
		while (reader.read_row(name, addr, size, bitmask, unit, isSigned, isInverseConversion, factor, offset, description)) {
			_parameters.emplace_back(name, std::stol(addr, nullptr, 16), size, std::stol(bitmask, nullptr, 16), unit,
				(isSigned > 0), (isInverseConversion > 0), factor, offset, description);
		}
	}

	LogParameters::LogParameters(const LogParameters& rhs)
		: _parameters{rhs._parameters}
	{
	}

	const LogParameters& LogParameters::operator=(const LogParameters& rhs)
	{
		if (this == &rhs)
			return *this;

		_parameters = rhs._parameters;

		return *this;
	}

	const std::vector<LogParameter>& LogParameters::parameters() const
	{
		return _parameters;
	}

} // namespace logger
