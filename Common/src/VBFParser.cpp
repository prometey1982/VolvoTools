#include "common/VBFParser.hpp"

#include <boost/spirit/home/x3.hpp>
#include <boost/fusion/include/adapt_struct.hpp>
#include <boost/spirit/home/x3/support/utility/annotate_on_success.hpp>
#include <boost/spirit/home/x3/support/utility/error_reporting.hpp>

#include <string>
#include <vector>
#include <unordered_set>
#include <iostream>
#include <utility>

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
		x3::rule<class erase, std::vector<std::pair<uint32_t, uint32_t>>> const erase = "erase";

		auto const quoted_string = x3::lexeme['"' >> *(x3::char_ - '"' - x3::eol) >> '"'];
		auto const unquoted_string = x3::lexeme[+(x3::char_ - ';' - x3::eol)];

		auto const hex = x3::lit("0x") >> x3::hex;

		auto const space_comment = x3::space | x3::lexeme["//" >> *(x3::char_ - x3::eol) >> x3::eol];

		auto const erase_block = '{' >> hex >> ',' >> hex >> '}';

		auto const erase_def = x3::lit("erase") >> '=' >> '{' >> (erase_block % ',') >> '}' >> ';';

		auto const description_def = x3::lit("description") >> '=' >> '{' >> (quoted_string % ',') >> '}' >> ';';
		auto const vbf_header_def = 
			x3::lit("vbf_version") >> '=' >> x3::float_[([](auto& ctx) {
			x3::_val(ctx).vbfVersion = x3::_attr(ctx);
				})] >> ';' >>
			x3::lit("header") >> '{' >> *(description[([](auto& ctx) {
					x3::_val(ctx).description = x3::_attr(ctx);
				})]
//				| erase[([](auto& ctx) { x3::_val(ctx).erase = x3::_attr(ctx); })]
				| (x3::lit("sw_part_number") >> '=' >> quoted_string[([](auto& ctx) {
					x3::_val(ctx).swPartNumber = x3::_attr(ctx);
					})] >> ';')
				| (x3::lit("sw_part_number") >> '=' >> unquoted_string[([](auto& ctx) {
					x3::_val(ctx).swPartNumber = x3::_attr(ctx);
					})] >> ';')
				| (x3::lit("sw_version") >> '=' >> unquoted_string[([](auto& ctx) {
					x3::_val(ctx).swVersion = x3::_attr(ctx);
					})] >> ';')
				| (x3::lit("sw_part_type") >> '=' >> unquoted_string[([](auto& ctx) {
					x3::_val(ctx).swPartType = parseSWPartType(x3::_attr(ctx));
					})] >> ';')
				| (x3::lit("network") >> '=' >> unquoted_string[([](auto& ctx) {
					x3::_val(ctx).network = parseNetworkType(x3::_attr(ctx));
					})] >> ';')
				| (x3::lit("ecu_address") >> '=' >> hex[([](auto& ctx) {
					x3::_val(ctx).ecuAddress = x3::_attr(ctx);
					})] >> ';')
				| (x3::lit("ecu_addr") >> '=' >> hex[([](auto& ctx) {
					x3::_val(ctx).ecuAddress = x3::_attr(ctx);
					})] >> ';')
				| (x3::lit("frame_format") >> '=' >> unquoted_string[([](auto& ctx) {
					x3::_val(ctx).frameFormat = parseFrameFormat(x3::_attr(ctx));
					})] >> ';')
				| (x3::lit("can_frame_format") >> '=' >> unquoted_string[([](auto& ctx) {
					x3::_val(ctx).frameFormat = parseFrameFormat(x3::_attr(ctx));
					})] >> ';')
				| (x3::lit("call") >> '=' >> hex[([](auto& ctx) {
					x3::_val(ctx).call = x3::_attr(ctx);
					})] >> ';')
				| (x3::lit("jmp") >> '=' >> hex[([](auto& ctx) {
					x3::_val(ctx).call = x3::_attr(ctx);
					})] >> ';')
				| (x3::lit("jsr") >> '=' >> hex[([](auto& ctx) {
					x3::_val(ctx).call = x3::_attr(ctx);
					})] >> ';')
				| (x3::lit("file_checksum") >> '=' >> hex[([](auto& ctx) {
					x3::_val(ctx).fileChecksum = x3::_attr(ctx);
					})] >> ';')) >> '}';

		BOOST_SPIRIT_DEFINE(description, vbf_header, erase);

		template<typename T>
		T parseVBFHeader(T begin, T end, VBFHeader& data)
		{
			if (!phrase_parse(begin, end, vbf_header, space_comment, data)) {
				throw std::runtime_error("Failed to parse VBF header");
			}

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
		std::vector<VBFChunk> parseVBFBody(const VBFHeader& header, T begin, T end)
		{
			std::vector<VBFChunk> result;
			while (begin < end) {
				uint32_t writeOffset = getUint32(begin);
				uint32_t dataSize = getUint32(begin);
				std::vector<uint8_t> data{ begin, begin + dataSize };
				begin += dataSize;
				uint16_t crc = header.vbfVersion >= 2 ? getUint16(begin) : static_cast<uint8_t>(*begin++);
				result.emplace_back(writeOffset, std::move(data), crc);
			}
			return result;
		}

		template<typename T>
		VBF parseVBF(T begin, T end)
		{
			VBFHeader vbfHeader;
			begin = parseVBFHeader(begin, end, vbfHeader);
			auto vbfChunks = parseVBFBody(vbfHeader, begin, end);
			return { vbfHeader, vbfChunks };
		}

	}

	VBF VBFParser::parse(std::istream& data) const
	{
		using namespace std;
		data.seekg(0, data.end);
		const istream::pos_type fileSize = data.tellg();
		data.seekg(0, ios::beg);
		std::vector<char> content(fileSize);
		data.read(content.data(), content.size());
		return parseVBF(content.begin(), content.end());
	}

	VBF VBFParser::parse(const std::vector<char>& data) const
	{
		return parseVBF(data.begin(), data.end());
	}

    VBF VBFParser::parse(const std::vector<uint8_t>& data) const
    {
        return parseVBF(reinterpret_cast<const char*>(data.data()), reinterpret_cast<const char*>(data.data()) + data.size());
    }

} // namespace common
