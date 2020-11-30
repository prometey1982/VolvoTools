#pragma once

#include "LogParameter.hpp"

#include <string>
#include <vector>

namespace logger
{

	class LogParameters final
	{
	public:
		LogParameters() = default;
		explicit LogParameters(const std::string& path);
		LogParameters(const LogParameters& rhs);

		const LogParameters& operator=(const LogParameters& rhs);

		const std::vector<LogParameter>& parameters() const;
		unsigned long getNumberOfCanMessages() const;

	private:
		std::vector<LogParameter> _parameters;
	};

} // namespace logger
