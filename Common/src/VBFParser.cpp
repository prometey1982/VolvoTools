#include "common/VBFParser.hpp"

#include <boost/spirit/home/x3.hpp>
#include <boost/fusion/include/adapt_struct.hpp>

#include <string>
#include <vector>
#include <unordered_set>
#include <iostream>

namespace common {

	namespace {

		std::string tolower(std::string data)
		{
			std::transform(data.begin(), data.end(), data.begin(),
				[](unsigned char c) { return std::tolower(c); });
			return data;
		}

		FrameFormat parseFrameFormat(const std::string& value)
		{
			const auto lowered = tolower(value);
			if (lowered == "can_standard")
				return FrameFormat::CAN_STANDARD;
			else if (lowered == "standard")
				return FrameFormat::CAN_STANDARD;
			else if (lowered == "can_extended")
				return FrameFormat::CAN_EXTENDED;
			else if (lowered == "extended")
				return FrameFormat::CAN_EXTENDED;
			return FrameFormat::UNKNOWN;
		}

		NetworkType parseNetworkType(const std::string& value)
		{
			const auto lowered = tolower(value);
			if (lowered == "can_hs")
				return NetworkType::CAN_HS;
			else if (lowered == "can_ms")
				return NetworkType::CAN_MS;
			return NetworkType::UNKNOWN;
		}

		SWPartType parseSWPartType(const std::string& value)
		{
			const auto lowered = tolower(value);
			if (lowered == "sbl")
				return SWPartType::SBL;
			else if (lowered == "data")
				return SWPartType::DATA;
			else if (lowered == "exe")
				return SWPartType::EXE;
			else if (lowered == "sigcfg")
				return SWPartType::SIGCFG;
			return SWPartType::UNKNOWN;
		}

		namespace x3 = boost::spirit::x3;

		x3::rule<class description, std::vector<std::string>> const description = "description";
		x3::rule<class vbf_header, VBFHeader> const vbf_header = "vbf_header";

		auto const quoted_string = x3::lexeme['"' >> *(x3::char_ - '"' - x3::eol) >> '"'];
		auto const unquoted_string = x3::lexeme[+(x3::char_ - ';' - x3::eol)];

		auto const space_comment = x3::space | x3::lexeme["//" >> *(x3::char_ - x3::eol) >> x3::eol];

		auto const description_def = x3::lit("description") >> '=' >> '{' >> (quoted_string % ',') >> '}' >> ';';
		auto const vbf_header_def =
			x3::lit("vbf_version") >> '=' >> x3::float_[([](auto& ctx) { x3::_val(ctx).vbfVersion = x3::_attr(ctx); })] >> ';' >>
			x3::lit("header") >> '{' >> *(description[([](auto& ctx) { x3::_val(ctx).description = x3::_attr(ctx); })]
				| (x3::lit("sw_part_number") >> '=' >> quoted_string[([](auto& ctx) { x3::_val(ctx).swPartNumber = x3::_attr(ctx); })] >> ';')
				| (x3::lit("sw_part_type") >> '=' >> unquoted_string[([](auto& ctx) { x3::_val(ctx).swPartType = parseSWPartType(x3::_attr(ctx)); })] >> ';')
				| (x3::lit("network") >> '=' >> unquoted_string[([](auto& ctx) { x3::_val(ctx).network = parseNetworkType(x3::_attr(ctx)); })] >> ';')
				| (x3::lit("ecu_address") >> '=' >> x3::lit("0x") >> x3::hex[([](auto& ctx) { x3::_val(ctx).ecuAddress = x3::_attr(ctx); })] >> ';')
				| (x3::lit("ecu_addr") >> '=' >> x3::lit("0x") >> x3::hex[([](auto& ctx) { x3::_val(ctx).ecuAddress = x3::_attr(ctx); })] >> ';')
				| (x3::lit("frame_format") >> '=' >> unquoted_string[([](auto& ctx) { x3::_val(ctx).frameFormat = parseFrameFormat(x3::_attr(ctx)); })] >> ';')
				| (x3::lit("can_frame_format") >> '=' >> unquoted_string[([](auto& ctx) { x3::_val(ctx).frameFormat = parseFrameFormat(x3::_attr(ctx)); })] >> ';')
				| (x3::lit("call") >> '=' >> x3::lit("0x") >> x3::hex[([](auto& ctx) { x3::_val(ctx).call = x3::_attr(ctx); })] >> ';')
				| (x3::lit("jmp") >> '=' >> x3::lit("0x") >> x3::hex[([](auto& ctx) { x3::_val(ctx).call = x3::_attr(ctx); })] >> ';')
				| (x3::lit("file_checksum") >> '=' >> x3::lit("0x") >> x3::hex[([](auto& ctx) { x3::_val(ctx).fileChecksum = x3::_attr(ctx); })] >> ';')) >> '}';

		BOOST_SPIRIT_DEFINE(description, vbf_header);

		template<typename T>
		T parseVBFHeader(T begin, T end, VBFHeader& data)
		{
			bool r = phrase_parse(begin, end, vbf_header, space_comment, data);

			return begin;
		}

		template<typename T>
		uint32_t getUint32(T& iter)
		{
			uint8_t byte4 = *iter++;
			uint8_t byte3 = *iter++;
			uint8_t byte2 = *iter++;
			uint8_t byte1 = *iter++;
			return (byte4 << 24) + (byte3 << 16) + (byte2 << 8) + byte1;
		}

		template<typename T>
		uint16_t getUint16(T& iter)
		{
			uint8_t byte2 = *iter++;
			uint8_t byte1 = *iter++;
			return (byte2 << 8) + byte1;
		}

		template<typename T>
		std::vector<VBFChunk> parseVBFBody(T begin, T end)
		{
			std::vector<VBFChunk> result;
			while (begin != end) {
				uint32_t writeOffset = getUint32(begin);
				uint32_t dataSize = getUint32(begin);
				std::vector<uint8_t> data{ begin, begin + dataSize };
				begin += dataSize;
				uint16_t crc = getUint16(begin);
				result.emplace_back(writeOffset, std::move(data), crc);
			}
			return result;
		}

	}

	VBFParser::VBFParser()
	{
	}

	VBF VBFParser::parse(std::istream& data)
	{
		using namespace std;
		data.seekg(0, data.end);
		const istream::pos_type fileSize = data.tellg();
		data.seekg(0, ios::beg);
		std::vector<uint8_t> content(fileSize);
		data.read(reinterpret_cast<char*>(content.data()), content.size());
		auto iter = content.begin();
		auto end = content.end();
		VBFHeader vbfHeader;
		iter = parseVBFHeader(iter, end, vbfHeader);
		auto vbfChunks = parseVBFBody(iter, end);
		return { vbfHeader, vbfChunks };
	}

} // namespace common
