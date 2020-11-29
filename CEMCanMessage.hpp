#pragma once

#include <vector>
#include <cstdint>

namespace logger
{

	class CEMCanMessage
	{
	public:
		explicit CEMCanMessage(const std::vector<uint8_t>& data);

		std::vector<uint8_t> data() const;

	private:
		const std::vector<uint8_t> _data;
	};

} // namespace logger
