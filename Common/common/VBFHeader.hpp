#pragma once

#include <vector>
#include <string>

namespace common {

	enum class NetworkType {
		UNKNOWN,
		CAN_HS,
		CAN_MS
	};

	enum class FrameFormat {
		UNKNOWN,
		CAN_STANDARD,
		CAN_EXTENDED
	};

	enum class SWPartType {
		UNKNOWN,
		SBL,
		DATA,
		EXE,
		SIGCFG,
		CARCFG
	};

	enum class SessionType {
		DEFAULT,
		PROGRAMMING,
		EXTENDED,
		SAFETY_SYSTEM,
		OTHER
	};

	enum class FlashStrategy {
		INPLACE,
		SWAP,
		DUAL_BANK,
		BACKGROUND,
		TRI_BANK,
		SEQUENTIAL,
		OVERLAY,
		UNKNOWN
	};

    struct DataBlock
	{
		uint32_t startAddr;
		uint32_t length;
	};

	struct ChecksumBlock
	{
		uint32_t startAddr;
		uint32_t endAddr;
		uint32_t checksum;
	};

	struct VBFHeader {
		double vbfVersion{};
		std::vector<std::string> description;
		std::string swPartNumber;
		std::string swVersion;
		SWPartType swPartType{ SWPartType::UNKNOWN };
		NetworkType network{ NetworkType::UNKNOWN };
		uint32_t ecuAddress{};
		FrameFormat frameFormat{ FrameFormat::UNKNOWN };
		uint32_t call{};
		uint32_t fileChecksum{};
        std::vector<DataBlock> eraseBlocks;
		std::vector<ChecksumBlock> checksumTable;
		FlashStrategy flashStrategy{ FlashStrategy::INPLACE };
		SessionType sessionType{ SessionType::PROGRAMMING };
		uint8_t securityAccessLevel{ 0x27 };
		std::string signature;
		std::string certificateIdentifier;
	};

} // namespace common
