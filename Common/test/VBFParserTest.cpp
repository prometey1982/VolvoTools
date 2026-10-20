#include <boost/test/unit_test.hpp>

#include "common/VBFParser.hpp"

#include <cstdint>
#include <stdexcept>
#include <string>
#include <vector>

using namespace common;

namespace {

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

void appendUint32Be(std::vector<char>& out, uint32_t value)
{
    out.push_back(static_cast<char>((value >> 24) & 0xFF));
    out.push_back(static_cast<char>((value >> 16) & 0xFF));
    out.push_back(static_cast<char>((value >> 8) & 0xFF));
    out.push_back(static_cast<char>(value & 0xFF));
}

void appendUint16Be(std::vector<char>& out, uint16_t value)
{
    out.push_back(static_cast<char>((value >> 8) & 0xFF));
    out.push_back(static_cast<char>(value & 0xFF));
}

void appendText(std::vector<char>& out, const std::string& text)
{
    out.insert(out.end(), text.begin(), text.end());
}

// One binary block: start address, length, data, trailing checksum.
void appendBlock(std::vector<char>& out, uint32_t startAddr, const std::vector<uint8_t>& data, size_t checksumSize)
{
    appendUint32Be(out, startAddr);
    appendUint32Be(out, static_cast<uint32_t>(data.size()));
    out.insert(out.end(), data.begin(), data.end());
    if(checksumSize == sizeof(uint16_t)) {
        appendUint16Be(out, 0x1C07);
    }
    else {
        out.push_back(static_cast<char>(0x84));
    }
}

std::vector<uint8_t> makePayload(size_t size, uint8_t seed)
{
    std::vector<uint8_t> payload(size);
    for(size_t i = 0; i < payload.size(); ++i) {
        payload[i] = static_cast<uint8_t>(seed + i);
    }
    return payload;
}

// VBF 1.1 header the way VBF CONVERT 2.1 emits it: the checksum table holds flat
// "start, end, checksum" triples instead of braced groups (file 30771403_P1_ME9_exe.vbf).
std::vector<char> makeVbf11FlatChecksumTable()
{
    std::vector<char> vbf;
    appendText(vbf,
               "vbf_version=1.1;\r\n"
               "\r\n"
               "header {\r\n"
               "    // Volvo software part number\r\n"
               "       sw_part_number=30771403;\r\n"
               "    // Software version\r\n"
               "       sw_version=AB;\r\n"
               "    // Volvo software part type: Executable #1\r\n"
               "       sw_part_type=exe1;\r\n"
               "    // ECU address\r\n"
               "       ecu_addr=0x7A;\r\n"
               "    // Start address and parameter for flash sector(s)\r\n"
               "       erase=0x20000;\r\n"
               "    // Checksum table: Start  Stop   Checksum\r\n"
               "       checksum_table={0x000a0000,0x0019ffff,0xd9,\r\n"
               "                       0x000a1041,0x001a1040,0xed,\r\n"
               "                       0x000a2082,0x001a2081,0x1d};\r\n"
               "}\r\n");
    appendBlock(vbf, 0x00020000, makePayload(16, 0x00), 1);
    appendBlock(vbf, 0x000A0000, makePayload(32, 0x10), 1);
    return vbf;
}

// VBF 2.x header: the checksum table holds one braced group per triple and every
// block is followed by a 16-bit CRC.
std::vector<char> makeVbf2BracedChecksumTable()
{
    std::vector<char> vbf;
    appendText(vbf,
               "vbf_version=2.2;\r\n"
               "header {\r\n"
               "    description={\"test\"};\r\n"
               "    sw_part_number=\"6G92-14C093-AC\";\r\n"
               "    sw_part_type=SBL;\r\n"
               "    network=CAN_MS;\r\n"
               "    ecu_address=0x736;\r\n"
               "    frame_format=CAN_STANDARD;\r\n"
               "    erase={{0x00001000,0x00003FFF},{0x000060FF,0x00000500}};\r\n"
               "    checksum_table={{0x000a0000,0x0019ffff,0xd9},\r\n"
               "                    {0x000a1041,0x001a1040,0xed}};\r\n"
               "}\r\n");
    appendBlock(vbf, 0x00001000, makePayload(8, 0x01), 2);
    appendBlock(vbf, 0x00002000, makePayload(12, 0x02), 2);
    return vbf;
}

} // namespace

// ---------------------------------------------------------------------------

BOOST_AUTO_TEST_SUITE(VBFParserTest)

BOOST_AUTO_TEST_CASE(FlatChecksumTableIsParsed)
{
    const auto file = makeVbf11FlatChecksumTable();
    VBFParser parser;
    const auto vbf = parser.parse(file);

    BOOST_CHECK_EQUAL(vbf.header.vbfVersion, 1.1);
    BOOST_CHECK_EQUAL(vbf.header.swPartNumber, "30771403");
    BOOST_CHECK_EQUAL(vbf.header.swVersion, "AB");
    BOOST_CHECK(vbf.header.swPartType == SWPartType::EXE);
    BOOST_CHECK_EQUAL(vbf.header.ecuAddress, 0x7Au);

    BOOST_REQUIRE_EQUAL(vbf.header.eraseBlocks.size(), 1u);
    BOOST_CHECK_EQUAL(vbf.header.eraseBlocks[0].startAddr, 0x20000u);
    BOOST_CHECK_EQUAL(vbf.header.eraseBlocks[0].length, 0u);

    BOOST_REQUIRE_EQUAL(vbf.header.checksumTable.size(), 3u);
    BOOST_CHECK_EQUAL(vbf.header.checksumTable[0].startAddr, 0x000A0000u);
    BOOST_CHECK_EQUAL(vbf.header.checksumTable[0].endAddr, 0x0019FFFFu);
    BOOST_CHECK_EQUAL(vbf.header.checksumTable[0].checksum, 0xD9u);
    BOOST_CHECK_EQUAL(vbf.header.checksumTable[1].startAddr, 0x000A1041u);
    BOOST_CHECK_EQUAL(vbf.header.checksumTable[1].endAddr, 0x001A1040u);
    BOOST_CHECK_EQUAL(vbf.header.checksumTable[1].checksum, 0xEDu);
    BOOST_CHECK_EQUAL(vbf.header.checksumTable[2].startAddr, 0x000A2082u);
    BOOST_CHECK_EQUAL(vbf.header.checksumTable[2].endAddr, 0x001A2081u);
    BOOST_CHECK_EQUAL(vbf.header.checksumTable[2].checksum, 0x1Du);

    BOOST_REQUIRE_EQUAL(vbf.chunks.size(), 2u);
    BOOST_CHECK_EQUAL(vbf.chunks[0].writeOffset, 0x00020000u);
    BOOST_CHECK_EQUAL(vbf.chunks[0].data.size(), 16u);
    BOOST_CHECK_EQUAL(vbf.chunks[0].data[0], 0x00u);
    BOOST_CHECK_EQUAL(vbf.chunks[0].crc, 0x84u); // one byte for vbf_version 1.x
    BOOST_CHECK_EQUAL(vbf.chunks[1].writeOffset, 0x000A0000u);
    BOOST_CHECK_EQUAL(vbf.chunks[1].data.size(), 32u);
    BOOST_CHECK_EQUAL(vbf.chunks[1].data[1], 0x11u);
    BOOST_CHECK_EQUAL(vbf.chunks[1].crc, 0x84u);
}

BOOST_AUTO_TEST_CASE(BracedChecksumTableIsStillParsed)
{
    const auto file = makeVbf2BracedChecksumTable();
    VBFParser parser;
    const auto vbf = parser.parse(file);

    BOOST_CHECK_EQUAL(vbf.header.vbfVersion, 2.2);
    BOOST_CHECK_EQUAL(vbf.header.swPartNumber, "6G92-14C093-AC");
    BOOST_CHECK(vbf.header.swPartType == SWPartType::SBL);
    BOOST_CHECK(vbf.header.network == NetworkType::CAN_MS);
    BOOST_CHECK_EQUAL(vbf.header.ecuAddress, 0x736u);

    BOOST_REQUIRE_EQUAL(vbf.header.eraseBlocks.size(), 2u);
    BOOST_CHECK_EQUAL(vbf.header.eraseBlocks[0].startAddr, 0x1000u);
    BOOST_CHECK_EQUAL(vbf.header.eraseBlocks[0].length, 0x3FFFu);
    BOOST_CHECK_EQUAL(vbf.header.eraseBlocks[1].startAddr, 0x60FFu);
    BOOST_CHECK_EQUAL(vbf.header.eraseBlocks[1].length, 0x500u);

    BOOST_REQUIRE_EQUAL(vbf.header.checksumTable.size(), 2u);
    BOOST_CHECK_EQUAL(vbf.header.checksumTable[0].startAddr, 0x000A0000u);
    BOOST_CHECK_EQUAL(vbf.header.checksumTable[1].checksum, 0xEDu);

    BOOST_REQUIRE_EQUAL(vbf.chunks.size(), 2u);
    BOOST_CHECK_EQUAL(vbf.chunks[0].writeOffset, 0x1000u);
    BOOST_CHECK_EQUAL(vbf.chunks[0].data.size(), 8u);
    BOOST_CHECK_EQUAL(vbf.chunks[0].crc, 0x1C07u); // two bytes for vbf_version 2.x
    BOOST_CHECK_EQUAL(vbf.chunks[1].writeOffset, 0x2000u);
    BOOST_CHECK_EQUAL(vbf.chunks[1].data.size(), 12u);
    BOOST_CHECK_EQUAL(vbf.chunks[1].crc, 0x1C07u);
}

BOOST_AUTO_TEST_CASE(EmptyChecksumTableIsParsed)
{
    std::vector<char> file;
    appendText(file,
               "vbf_version=1.1;\r\n"
               "header {\r\n"
               "    sw_part_number=30771403;\r\n"
               "    checksum_table={};\r\n"
               "}\r\n");
    appendBlock(file, 0x00020000, makePayload(4, 0x00), 1);

    VBFParser parser;
    const auto vbf = parser.parse(file);
    BOOST_CHECK(vbf.header.checksumTable.empty());
    BOOST_REQUIRE_EQUAL(vbf.chunks.size(), 1u);
    BOOST_CHECK_EQUAL(vbf.chunks[0].writeOffset, 0x20000u);
}

BOOST_AUTO_TEST_CASE(HeaderOnlyFileHasNoChunks)
{
    std::vector<char> file;
    appendText(file,
               "vbf_version=1.1;\r\n"
               "header {\r\n"
               "    sw_part_number=30771403;\r\n"
               "    checksum_table={0x000a0000,0x0019ffff,0xd9};\r\n"
               "}\r\n");

    VBFParser parser;
    const auto vbf = parser.parse(file);
    BOOST_REQUIRE_EQUAL(vbf.header.checksumTable.size(), 1u);
    BOOST_CHECK(vbf.chunks.empty());
}

BOOST_AUTO_TEST_CASE(TruncatedBodyThrows)
{
    auto file = makeVbf11FlatChecksumTable();
    file.resize(file.size() - 4);

    VBFParser parser;
    BOOST_REQUIRE_THROW(parser.parse(file), std::runtime_error);
}

BOOST_AUTO_TEST_CASE(InvalidHeaderThrows)
{
    std::vector<char> file;
    appendText(file, "vbf_version=1.1;\r\nheader {\r\n    sw_part_number=30771403;\r\n");

    VBFParser parser;
    BOOST_REQUIRE_THROW(parser.parse(file), std::runtime_error);
}

BOOST_AUTO_TEST_SUITE_END()
