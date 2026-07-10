#define BOOST_TEST_MODULE D2Message
#include <boost/test/unit_test.hpp>

#include <easylogging++.h>
INITIALIZE_EASYLOGGINGPP

#include "common/protocols/D2Message.hpp"
#include "common/CanFrame.hpp"

#include <algorithm>
#include <array>
#include <cstdint>
#include <vector>

using namespace common;
using DataType = common::CanMessage::DataType;

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

static void checkPrefix(uint8_t actual, uint8_t expected)
{
    BOOST_CHECK_EQUAL(actual, expected);
}

static void checkFrameSize(const std::vector<CanFrame>& frames, size_t expected)
{
    BOOST_CHECK_EQUAL(frames.size(), expected);
}

static void checkPayloadValue(const CanFrame& frame, size_t index, uint8_t expected)
{
    BOOST_REQUIRE(index < frame.data.size());
    BOOST_CHECK_EQUAL(frame.data[index], expected);
}

static void checkPadding(const CanFrame& frame, size_t from, size_t to)
{
    for (size_t i = from; i < to && i < frame.data.size(); ++i) {
        BOOST_CHECK_EQUAL(frame.data[i], 0);
    }
}

static void checkCanId(const CanFrame& frame)
{
    BOOST_CHECK_EQUAL(frame.id, D2Message::CanId);
    BOOST_CHECK(frame.isExtendedId);
}

static void checkPayloadRange(const CanFrame& frame, size_t offset,
                               const std::vector<uint8_t>& expected)
{
    for (size_t j = 0; j < expected.size(); ++j) {
        BOOST_REQUIRE(offset + j < frame.data.size());
        BOOST_CHECK_EQUAL(frame.data[offset + j], expected[j]);
    }
}

// ---------------------------------------------------------------------------
// 1. Constructors
// ---------------------------------------------------------------------------

BOOST_AUTO_TEST_CASE(ConstructorRawData)
{
    D2Message msg{DataType{0x50, 0xB9, 0xFB}};
    auto frames = msg.getFrames();
    BOOST_CHECK(!frames.empty());
}

BOOST_AUTO_TEST_CASE(ConstructorEcuIdRequestId)
{
    D2Message msg{0x50, {0xB9, 0xFB}};
    auto frames = msg.getFrames();
    BOOST_CHECK(!frames.empty());
}

BOOST_AUTO_TEST_CASE(ConstructorEcuIdRequestIdParams)
{
    D2Message msg{0x50, {0xB7}, {0x01, 0x02}};
    auto frames = msg.getFrames();
    BOOST_CHECK(!frames.empty());
}

BOOST_AUTO_TEST_CASE(ConstructorMove)
{
    D2Message original{0x50, {0xB9, 0xFB}};
    D2Message moved{std::move(original)};
    auto frames = moved.getFrames();
    BOOST_CHECK(!frames.empty());
}

// ---------------------------------------------------------------------------
// 2. Getters
// ---------------------------------------------------------------------------

BOOST_AUTO_TEST_CASE(GetEcuId)
{
    D2Message msg{0x50, {0xB9, 0xFB}};
    BOOST_CHECK_EQUAL(msg.getEcuId(), 0x50);
}

BOOST_AUTO_TEST_CASE(GetRequestId)
{
    D2Message msg{0x50, {0xB9, 0xFB}};
    auto rid = msg.getRequestId();
    BOOST_CHECK_EQUAL(rid.size(), 2);
    BOOST_CHECK_EQUAL(rid[0], 0xB9);
    BOOST_CHECK_EQUAL(rid[1], 0xFB);
}

BOOST_AUTO_TEST_CASE(GetEcuIdRaw)
{
    D2Message msg{DataType{0x50, 0xB9, 0xFB}};
    BOOST_CHECK_EQUAL(msg.getEcuId(), 0);
}

BOOST_AUTO_TEST_CASE(GetRequestIdRaw)
{
    D2Message msg{DataType{0x50, 0xB9, 0xFB}};
    BOOST_CHECK(!msg.getRequestId().empty());
}

// ---------------------------------------------------------------------------
// 3. Single-frame (dataSize ≤ 7)
// ---------------------------------------------------------------------------

BOOST_AUTO_TEST_CASE(SingleFrame1Byte)
{
    D2Message msg{0x50, {0xB9}};
    auto frames = msg.getFrames();
    checkFrameSize(frames, 1);
    checkCanId(frames[0]);
    checkPrefix(frames[0].data[0], 0xCA);
    checkPayloadValue(frames[0], 1, 0x50);
    checkPayloadValue(frames[0], 2, 0xB9);
    checkPadding(frames[0], 3, 8);
}

BOOST_AUTO_TEST_CASE(SingleFrame7Bytes)
{
    D2Message msg{0x50, {0xB9, 0xFB, 0x01, 0x02, 0x03, 0x04}};
    auto frames = msg.getFrames();
    checkFrameSize(frames, 1);
    checkCanId(frames[0]);
    checkPrefix(frames[0].data[0], 0xCF);
    checkPayloadValue(frames[0], 1, 0x50);
    checkPayloadValue(frames[0], 2, 0xB9);
    checkPayloadValue(frames[0], 7, 0x04);
}

BOOST_AUTO_TEST_CASE(SingleFramePrefix)
{
    D2Message msg{0x50, {0xB9, 0xFB}};
    auto frames = msg.getFrames();
    checkPrefix(frames[0].data[0], 0xCB);
}

BOOST_AUTO_TEST_CASE(SingleFrameEcuId)
{
    D2Message msg{0x7A, {0xB9}};
    auto frames = msg.getFrames();
    checkPayloadValue(frames[0], 1, 0x7A);
}

BOOST_AUTO_TEST_CASE(SingleFrameRequestId)
{
    D2Message msg{0x50, {0xB9, 0xFB}};
    auto frames = msg.getFrames();
    checkPayloadValue(frames[0], 2, 0xB9);
    checkPayloadValue(frames[0], 3, 0xFB);
}

BOOST_AUTO_TEST_CASE(SingleFrameParams)
{
    D2Message msg{0x50, {0xB7}, {0x01, 0x02}};
    auto frames = msg.getFrames();
    checkPayloadValue(frames[0], 2, 0xB7);
    checkPayloadValue(frames[0], 3, 0x01);
    checkPayloadValue(frames[0], 4, 0x02);
    checkPadding(frames[0], 5, 8);
}

BOOST_AUTO_TEST_CASE(SingleFramePadding)
{
    D2Message msg{0x50, {0xB9}};
    auto frames = msg.getFrames();
    checkPadding(frames[0], 3, 8);
}

// ---------------------------------------------------------------------------
// 4. Multi-frame (dataSize > 7)
// ---------------------------------------------------------------------------

BOOST_AUTO_TEST_CASE(MultiFrame2Frames)
{
    D2Message msg{0x50, {0xB9, 0xFB}, {0x01, 0x02, 0x03, 0x04, 0x05}};
    auto frames = msg.getFrames();
    checkFrameSize(frames, 2);
}

BOOST_AUTO_TEST_CASE(MultiFrame3Frames)
{
    std::vector<uint8_t> params(13, 0xAA);
    D2Message msg{0x50, {0xB9}, params};
    auto frames = msg.getFrames();
    checkFrameSize(frames, 3);
}

BOOST_AUTO_TEST_CASE(MultiFrameExact7n)
{
    std::vector<uint8_t> params(6, 0xAA);
    D2Message msg{0x50, {0xB9, 0xFB}, params};
    auto frames = msg.getFrames();
    checkFrameSize(frames, 2);
}

// ---------------------------------------------------------------------------
// 5. Prefixes in multi-frame
// ---------------------------------------------------------------------------

BOOST_AUTO_TEST_CASE(FirstFramePrefixMulti)
{
    D2Message msg{0x50, {0xB9, 0xFB}, {0x01, 0x02, 0x03, 0x04, 0x05}};
    auto frames = msg.getFrames();
    checkPrefix(frames[0].data[0], 0x8F);
}

BOOST_AUTO_TEST_CASE(LastFramePrefix)
{
    D2Message msg{0x50, {0xB9, 0xFB}, {0x01, 0x02, 0x03, 0x04, 0x05}};
    auto frames = msg.getFrames();
    BOOST_REQUIRE(frames.size() >= 2);
    checkPrefix(frames[1].data[0], 0x48 + 1);
}

BOOST_AUTO_TEST_CASE(FirstFramePayloadSizeMulti)
{
    D2Message msg{0x50, {0xB9, 0xFB}, {0x01, 0x02, 0x03, 0x04, 0x05}};
    auto frames = msg.getFrames();
    BOOST_CHECK_EQUAL(frames[0].data[0] & 0x07, 7);
}

// ---------------------------------------------------------------------------
// 6. SeriesId sequence
// ---------------------------------------------------------------------------

BOOST_AUTO_TEST_CASE(SeriesIdSequence)
{
    std::vector<uint8_t> params(20, 0xAA);
    D2Message msg{0x50, {0xB9}, params};
    auto frames = msg.getFrames();
    BOOST_REQUIRE(frames.size() >= 3);
    checkPrefix(frames[1].data[0], 0x09);
    checkPrefix(frames[2].data[0], 0x0A);
}

BOOST_AUTO_TEST_CASE(SeriesIdWrapAround)
{
    std::vector<uint8_t> params(69, 0xAA);
    D2Message msg{0x50, {0xB9}, params};
    auto frames = msg.getFrames();
    BOOST_REQUIRE_GE(frames.size(), 11);
    checkPrefix(frames[1].data[0], 0x09);
    checkPrefix(frames[7].data[0], 0x0F);
    checkPrefix(frames[8].data[0], 0x08);
    checkPrefix(frames[9].data[0], 0x09);
}

BOOST_AUTO_TEST_CASE(SeriesIdNoGap)
{
    std::vector<uint8_t> params(14, 0xAA);
    D2Message msg{0x50, {0xB9}, params};
    auto frames = msg.getFrames();
    BOOST_REQUIRE(frames.size() >= 3);
    checkPrefix(frames[1].data[0], 0x09);
}

// ---------------------------------------------------------------------------
// 7. Edge cases
// ---------------------------------------------------------------------------

BOOST_AUTO_TEST_CASE(DataSizeExact7)
{
    D2Message msg{0x50, {0xB9, 0xFB}, {0x01, 0x02, 0x03, 0x04}};
    auto frames = msg.getFrames();
    checkFrameSize(frames, 1);
}

BOOST_AUTO_TEST_CASE(DataSize8)
{
    D2Message msg{0x50, {0xB9, 0xFB}, {0x01, 0x02, 0x03, 0x04, 0x05}};
    auto frames = msg.getFrames();
    checkFrameSize(frames, 2);
}

BOOST_AUTO_TEST_CASE(ParamsEmptySingle)
{
    D2Message msg{0x50, {0xB9, 0xFB}};
    auto frames = msg.getFrames();
    checkFrameSize(frames, 1);
    checkPayloadValue(frames[0], 1, 0x50);
    checkPayloadValue(frames[0], 2, 0xB9);
    checkPayloadValue(frames[0], 3, 0xFB);
}

BOOST_AUTO_TEST_CASE(LongRequestIdMulti)
{
    std::vector<uint8_t> requestId(10, 0xBB);
    D2Message msg{0x50, requestId};
    auto frames = msg.getFrames();
    BOOST_REQUIRE(frames.size() > 1);
    checkPayloadValue(frames[0], 1, 0x50);
    checkPayloadRange(frames[0], 2, {requestId.begin(), requestId.begin() + 6});
}
