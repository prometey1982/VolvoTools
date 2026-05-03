#include "common/VBFParser.hpp"
#include "common/Util.hpp"

#include <boost/spirit/home/x3.hpp>
#include <boost/fusion/include/adapt_struct.hpp>
#include <boost/spirit/home/x3/support/utility/annotate_on_success.hpp>
#include <boost/spirit/home/x3/support/utility/error_reporting.hpp>

#include <iterator>
#include <string>
#include <vector>
#include <unordered_set>
#include <iostream>
#include <utility>

BOOST_FUSION_ADAPT_STRUCT(common::EraseBlock,
	(uint32_t, startAddr)
	(uint32_t, length)
)

BOOST_FUSION_ADAPT_STRUCT(common::ChecksumBlock,
	(uint32_t, startAddr)
	(uint32_t, endAddr)
	(uint32_t, checksum)
)

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
			else if (lowered == "exe1")
				return SWPartType::EXE;
			else if (lowered == "exe2")
				return SWPartType::EXE;
			else if (lowered == "sigcfg")
				return SWPartType::SIGCFG;
			else if (lowered == "carcfg")
				return SWPartType::CARCFG;
			return SWPartType::UNKNOWN;
		}

		SessionType parseSessionType(const std::string& value)
		{
			const auto lowered = tolower(value);
			if (lowered == "default")
				return SessionType::DEFAULT;
			else if (lowered == "programming")
				return SessionType::PROGRAMMING;
			else if (lowered == "extended")
				return SessionType::EXTENDED;
			else if (lowered == "safety_system")
				return SessionType::SAFETY_SYSTEM;
			return SessionType::OTHER;
		}

		FlashStrategy parseFlashStrategy(const std::string& value)
		{
			const auto lowered = tolower(value);
			if (lowered == "inplace")
				return FlashStrategy::INPLACE;
			else if (lowered == "swap")
				return FlashStrategy::SWAP;
			else if (lowered == "dual_bank")
				return FlashStrategy::DUAL_BANK;
			else if (lowered == "tri_bank")
				return FlashStrategy::TRI_BANK;
			else if (lowered == "background")
				return FlashStrategy::BACKGROUND;
			else if (lowered == "sequential")
				return FlashStrategy::SEQUENTIAL;
			else if (lowered == "overlay")
				return FlashStrategy::OVERLAY;
			return FlashStrategy::UNKNOWN;
		}

		namespace x3 = boost::spirit::x3;

		x3::rule<class description, std::vector<std::string>> const description = "description";
		x3::rule<class vbf_header, VBFHeader> const vbf_header = "vbf_header";
		x3::rule<class uint_rule, uint32_t> const uint_literal = "uint_literal";
		x3::rule<class uint8_rule, uint8_t> const uint8_literal = "uint8_literal";
		x3::rule<class full_erase_block, EraseBlock> const full_erase_block = "full_erase_block";
		x3::rule<class erase_block_list, std::vector<EraseBlock>> const erase_block_list = "erase_block_list";
		x3::rule<class checksum_block, ChecksumBlock> const checksum_block = "checksum_block";
		x3::rule<class checksum_block_list, std::vector<ChecksumBlock>> const checksum_block_list = "checksum_block_list";

		auto const quoted_string = x3::lexeme['"' >> *(x3::char_ - '"' - x3::eol) >> '"'];
		auto const unquoted_string = x3::lexeme[+(x3::char_ - ';' - x3::eol)];

		auto const uint_literal_def = x3::lit("0x") >> x3::uint_parser<uint32_t, 16>{}
			| x3::uint_parser<uint32_t, 10>{};
		auto const uint8_literal_def = x3::lit("0x") >> x3::uint_parser<uint8_t, 16>{}
			| x3::uint_parser<uint8_t, 10>{};

		auto const space_comment = x3::space | x3::lexeme["//" >> *(x3::char_ - x3::eol) >> x3::eol];

		auto const full_erase_block_def =
			('{' >> uint_literal >> ',' >> uint_literal >> '}');

		auto const erase_block_list_def = '{' >> -(full_erase_block % ',') >> '}';

		auto const on_erase_list = [](auto& ctx) {
			auto blocks = x3::_attr(ctx);
			auto& hdr = x3::_val(ctx);
			hdr.eraseBlocks.insert(hdr.eraseBlocks.end(), blocks.begin(), blocks.end());
		};

		auto const checksum_block_def =
			('{' >> uint_literal >> ',' >> uint_literal >> ',' >> uint_literal >> '}');

		auto const checksum_block_list_def = '{' >> -(checksum_block % ',') >> '}';

		auto const on_checksum_list = [](auto& ctx) {
			auto blocks = x3::_attr(ctx);
			auto& hdr = x3::_val(ctx);
			hdr.checksumTable.insert(hdr.checksumTable.end(), blocks.begin(), blocks.end());
			};

		auto const description_def = x3::lit("description") >> '=' >> '{' >> (quoted_string % ',') >> '}' >> ';';

		auto const vbf_header_def = 
			x3::lit("vbf_version") >> '=' >> x3::float_[([](auto& ctx) {
			x3::_val(ctx).vbfVersion = x3::_attr(ctx);
				})] >> ';' >>
			x3::lit("header") >> '{' >> *(description[([](auto& ctx) {
					x3::_val(ctx).description = x3::_attr(ctx);
				})]
				| (x3::lit("sw_part_number") >> '=' >> quoted_string[([](auto& ctx) {
					x3::_val(ctx).swPartNumber = x3::_attr(ctx);
					})] >> ';')
				| (x3::lit("sw_part_number") >> '=' >> unquoted_string[([](auto& ctx) {
					x3::_val(ctx).swPartNumber = x3::_attr(ctx);
					})] >> ';')
				| (x3::lit("erase") >> '=' >> uint_literal[([](auto& ctx) {
					x3::_val(ctx).eraseBlocks.push_back(EraseBlock(x3::_attr(ctx), 0));
					})] >> ';')
				| (x3::lit("erase") >> '=' >> erase_block_list[on_erase_list] >> ';')
				| (x3::lit("sw_version") >> '=' >> unquoted_string[([](auto& ctx) {
					x3::_val(ctx).swVersion = x3::_attr(ctx);
					})] >> ';')
				| (x3::lit("sw_part_type") >> '=' >> unquoted_string[([](auto& ctx) {
					x3::_val(ctx).swPartType = parseSWPartType(x3::_attr(ctx));
					})] >> ';')
				| (x3::lit("network") >> '=' >> unquoted_string[([](auto& ctx) {
					x3::_val(ctx).network = parseNetworkType(x3::_attr(ctx));
					})] >> ';')
				| (x3::lit("ecu_address") >> '=' >> uint_literal[([](auto& ctx) {
					x3::_val(ctx).ecuAddress = x3::_attr(ctx);
					})] >> ';')
				| (x3::lit("ecu_addr") >> '=' >> uint_literal[([](auto& ctx) {
					x3::_val(ctx).ecuAddress = x3::_attr(ctx);
					})] >> ';')
				| (x3::lit("erase_parameter") >> '=' >> uint_literal[([]([[maybe_unused]]auto& ctx) {
					})] >> ';')
				| (x3::lit("frame_format") >> '=' >> unquoted_string[([](auto& ctx) {
					x3::_val(ctx).frameFormat = parseFrameFormat(x3::_attr(ctx));
					})] >> ';')
				| (x3::lit("flash_strategy") >> '=' >> unquoted_string[([](auto& ctx) {
					x3::_val(ctx).flashStrategy = parseFlashStrategy(x3::_attr(ctx));
					})] >> ';')
				| (x3::lit("session_type") >> '=' >> unquoted_string[([](auto& ctx) {
					x3::_val(ctx).sessionType = parseSessionType(x3::_attr(ctx));
					})] >> ';')
				| (x3::lit("can_frame_format") >> '=' >> unquoted_string[([](auto& ctx) {
					x3::_val(ctx).frameFormat = parseFrameFormat(x3::_attr(ctx));
					})] >> ';')
				| (x3::lit("signature") >> '=' >> quoted_string[([](auto& ctx) {
					x3::_val(ctx).signature = x3::_attr(ctx);
					})] >> ';')
				| (x3::lit("certificate_identifier") >> '=' >> quoted_string[([](auto& ctx) {
					x3::_val(ctx).certificateIdentifier = x3::_attr(ctx);
					})] >> ';')
				| (x3::lit("call") >> '=' >> uint_literal[([](auto& ctx) {
					x3::_val(ctx).call = x3::_attr(ctx);
					})] >> ';')
				| (x3::lit("jmp") >> '=' >> uint_literal[([](auto& ctx) {
					x3::_val(ctx).call = x3::_attr(ctx);
					})] >> ';')
				| (x3::lit("jsr") >> '=' >> uint_literal[([](auto& ctx) {
					x3::_val(ctx).call = x3::_attr(ctx);
					})] >> ';')
				| (x3::lit("security_access_level") >> '=' >> uint8_literal[([](auto& ctx) {
					x3::_val(ctx).securityAccessLevel = x3::_attr(ctx);
					})] >> ';')
				| (x3::lit("checksum_table") >> '=' >> checksum_block_list[on_checksum_list] >> ';')
				| (x3::lit("file_checksum") >> '=' >> uint_literal[([](auto& ctx) {
					x3::_val(ctx).fileChecksum = x3::_attr(ctx);
					})] >> ';')) >> '}';

		BOOST_SPIRIT_DEFINE(description, vbf_header, uint_literal, uint8_literal,
							full_erase_block, erase_block_list,
							checksum_block, checksum_block_list);

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
		size_t remaining(T begin, T end)
		{
			return static_cast<size_t>(std::distance(begin, end));
		}

		template<typename T>
		std::vector<VBFChunk> parseVBFBody(const VBFHeader& header, T begin, T end)
		{
			std::vector<VBFChunk> result;
			const size_t checksumSize = header.vbfVersion >= 2 ? 2 : 1;
			while (begin < end) {
				if (remaining(begin, end) < 8) {
					throw std::runtime_error("Truncated VBF block header");
				}

				uint32_t writeOffset = getUint32(begin);
				uint32_t dataSize = getUint32(begin);

				if (remaining(begin, end) < dataSize + checksumSize) {
					throw std::runtime_error("Truncated VBF block data");
				}

				std::vector<uint8_t> data{ begin, begin + dataSize };
				begin += dataSize;
				uint16_t crc = header.vbfVersion >= 2 ? getUint16(begin) : static_cast<uint8_t>(*begin++);
				if (header.vbfVersion >= 2 && crc16(data.data(), data.size()) != crc) {
					throw std::runtime_error("VBF block CRC mismatch");
				}
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
