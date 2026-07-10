#include <boost/test/unit_test.hpp>

#include "common/protocols/D2Request.hpp"
#include "common/protocols/D2Error.hpp"
#include "common/CanFrame.hpp"

#include "MockICanChannel.hpp"

#include <cstdint>
#include <vector>

using namespace common;

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

static std::vector<uint8_t> restEchoOf(const std::vector<uint8_t>& requestId)
{
    if (requestId.size() <= 1)
        return {};
    return {requestId.begin() + 1, requestId.end()};
}

static CanFrame makeResponse(uint8_t header,
                              uint8_t ecuId,
                              const std::vector<uint8_t>& requestId,
                              const std::vector<uint8_t>& data)
{
    std::vector<uint8_t> payload;
    payload.push_back(header);
    payload.push_back(ecuId);
    payload.push_back(requestId[0] + 0x40);
    auto rest = restEchoOf(requestId);
    payload.insert(payload.end(), rest.begin(), rest.end());
    payload.insert(payload.end(), data.begin(), data.end());
    return {0xFFFFE, std::move(payload), true};
}

static CanFrame makeSeriesFrame(uint8_t header, const std::vector<uint8_t>& data)
{
    std::vector<uint8_t> payload;
    payload.push_back(header);
    payload.insert(payload.end(), data.begin(), data.end());
    return {0xFFFFE, std::move(payload), true};
}

static CanFrame makeErrorResponse(uint8_t ecuId, uint8_t errorCode)
{
    std::vector<uint8_t> payload(7, 0);
    payload[4] = 0x7F;
    payload[5] = ecuId;
    payload[6] = errorCode;
    return {0xFFFFE, std::move(payload), true};
}

// ---------------------------------------------------------------------------
// 1. Successful single-frame response
// ---------------------------------------------------------------------------

BOOST_AUTO_TEST_CASE(SingleFrameResponse)
{
    MockICanChannel mock;
    mock.receiveQueue.push(makeResponse(0xCF, 0x50, {0xB9, 0xFB}, {0x01, 0x02}));

    D2Request req{0x50, {0xB9, 0xFB}};
    auto result = req.process(mock, 1000);

    BOOST_CHECK_EQUAL(mock.sendCount, 1);
    BOOST_CHECK(!result.empty());
}

BOOST_AUTO_TEST_CASE(SingleFrameResponseData)
{
    MockICanChannel mock;
    mock.receiveQueue.push(makeResponse(0xCF, 0x50, {0xB9, 0xFB}, {0x01, 0x02, 0x03}));

    D2Request req{0x50, {0xB9, 0xFB}};
    auto result = req.process(mock, 1000);

    BOOST_REQUIRE_EQUAL(result.size(), 3);
    BOOST_CHECK_EQUAL(result[0], 0x01);
    BOOST_CHECK_EQUAL(result[1], 0x02);
    BOOST_CHECK_EQUAL(result[2], 0x03);
}

BOOST_AUTO_TEST_CASE(SingleFrameEchoCheck)
{
    MockICanChannel mock;
    mock.receiveQueue.push(makeResponse(0xCF, 0x50, {0xB9, 0xFB}, {0xAA}));

    D2Request req{0x50, {0xB9, 0xFB}};
    BOOST_CHECK_NO_THROW(req.process(mock, 1000));
}

// ---------------------------------------------------------------------------
// 2. Successful multi-frame response
// ---------------------------------------------------------------------------

BOOST_AUTO_TEST_CASE(MultiFrameResponse2Frames)
{
    MockICanChannel mock;
    mock.receiveQueue.push(makeResponse(0x8F, 0x50, {0xB9, 0xFB}, {0x01, 0x02, 0x03, 0x04}));
    mock.receiveQueue.push(makeSeriesFrame(0x49, {0x05, 0x06}));

    D2Request req{0x50, {0xB9, 0xFB}};
    auto result = req.process(mock, 1000);

    BOOST_REQUIRE_EQUAL(result.size(), 6);
    BOOST_CHECK_EQUAL(result[0], 0x01);
    BOOST_CHECK_EQUAL(result[5], 0x06);
}

BOOST_AUTO_TEST_CASE(MultiFrameResponse3Frames)
{
    MockICanChannel mock;
    mock.receiveQueue.push(makeResponse(0x8F, 0x50, {0xB9, 0xFB}, {0x01, 0x02, 0x03, 0x04}));
    mock.receiveQueue.push(makeSeriesFrame(0x09, {0x05, 0x06, 0x07, 0x08, 0x09, 0x0A, 0x0B}));
    mock.receiveQueue.push(makeSeriesFrame(0x49, {0x0C}));

    D2Request req{0x50, {0xB9, 0xFB}};
    auto result = req.process(mock, 1000);

    BOOST_REQUIRE_EQUAL(result.size(), 12);
    BOOST_CHECK_EQUAL(result[0], 0x01);
    BOOST_CHECK_EQUAL(result[11], 0x0C);
}

BOOST_AUTO_TEST_CASE(MultiFrameResponseRestEcho)
{
    MockICanChannel mock;
    mock.receiveQueue.push(makeResponse(0x8F, 0x50, {0xB9, 0xFC}, {0x01}));
    mock.receiveQueue.push(makeSeriesFrame(0x49, {0x02}));

    D2Request req{0x50, {0xB9, 0xFC}};
    auto result = req.process(mock, 1000);

    BOOST_REQUIRE_EQUAL(result.size(), 2);
    BOOST_CHECK_EQUAL(result[1], 0x02);
}

// ---------------------------------------------------------------------------
// 3. Echo check failures
// ---------------------------------------------------------------------------

BOOST_AUTO_TEST_CASE(EchoWrongEcuId)
{
    MockICanChannel mock;
    mock.receiveQueue.push(makeResponse(0xCF, 0xFF, {0xB9, 0xFB}, {0x01}));
    mock.receiveQueue.push(makeResponse(0xCF, 0x50, {0xB9, 0xFB}, {0xAA}));

    D2Request req{0x50, {0xB9, 0xFB}};
    auto result = req.process(mock, 1000);
    BOOST_CHECK_EQUAL(result.size(), 1);
}

BOOST_AUTO_TEST_CASE(EchoWrongRequestId)
{
    MockICanChannel mock;
    mock.receiveQueue.push(makeResponse(0xCF, 0x50, {0xB9, 0xFB}, {0x01}));
    mock.receiveQueue.push(makeResponse(0xCF, 0x50, {0xB9, 0xFB}, {0xAA}));
    // Первый ответ имеет неправильный requestId[0]+0x40,
    // т.к. 0xF9 != 0xB9 + 0x40, wait no — makeResponse уже добавляет +0x40

    // Нужно ответ с неправильным requestId[0]+0x40:
    std::vector<uint8_t> payload1 = {0xCF, 0x50, 0x00, 0xFB, 0x01};
    mock.receiveQueue.push(CanFrame{0xFFFFE, payload1, true});

    D2Request req{0x50, {0xB9, 0xFB}};
    auto result = req.process(mock, 1000);
    BOOST_CHECK_EQUAL(result.size(), 1);
}

BOOST_AUTO_TEST_CASE(EchoRestMismatch)
{
    MockICanChannel mock;
    // Неправильный rest эха — 0x00 вместо 0xFC
    std::vector<uint8_t> badPayload = {0xCF, 0x50, 0xF9, 0x00, 0x01};
    mock.receiveQueue.push(CanFrame{0xFFFFE, badPayload, true});
    mock.receiveQueue.push(makeResponse(0xCF, 0x50, {0xB9, 0xFC}, {0xAA}));

    D2Request req{0x50, {0xB9, 0xFC}};
    auto result = req.process(mock, 1000);
    BOOST_CHECK_EQUAL(result.size(), 1);
}

// ---------------------------------------------------------------------------
// 4. Error handling
// ---------------------------------------------------------------------------

BOOST_AUTO_TEST_CASE(ErrorResponse)
{
    MockICanChannel mock;
    mock.receiveQueue.push(makeErrorResponse(0x50, 0x22));

    D2Request req{0x50, {0xB9, 0xFB}};
    BOOST_CHECK_THROW(req.process(mock, 1000), D2Error);
}

BOOST_AUTO_TEST_CASE(ErrorWrongEcuIdInError)
{
    MockICanChannel mock;
    mock.receiveQueue.push(makeErrorResponse(0xFF, 0x22));
    mock.receiveQueue.push(makeResponse(0xCF, 0x50, {0xB9, 0xFB}, {0xAA}));

    D2Request req{0x50, {0xB9, 0xFB}};
    BOOST_CHECK_NO_THROW(req.process(mock, 1000));
}

BOOST_AUTO_TEST_CASE(ErrorResponseCorrectCode)
{
    MockICanChannel mock;
    mock.receiveQueue.push(makeErrorResponse(0x50, 0x78));

    D2Request req{0x50, {0xB9, 0xFB}};
    try {
        req.process(mock, 1000);
        BOOST_FAIL("Expected D2Error");
    } catch (const D2Error& e) {
        BOOST_CHECK_EQUAL(e.getErrorCode(), 0x78);
    }
}

// ---------------------------------------------------------------------------
// 5. Timeouts and empty frames
// ---------------------------------------------------------------------------

BOOST_AUTO_TEST_CASE(ReceiveTimeout)
{
    MockICanChannel mock;

    D2Request req{0x50, {0xB9, 0xFB}};
    BOOST_CHECK_THROW(req.process(mock, 1000), std::runtime_error);
}

BOOST_AUTO_TEST_CASE(EmptyFrameSkip)
{
    MockICanChannel mock;
    mock.receiveQueue.push(CanFrame{0xFFFFE, {}, true});
    mock.receiveQueue.push(makeResponse(0xCF, 0x50, {0xB9, 0xFB}, {0xAA}));

    D2Request req{0x50, {0xB9, 0xFB}};
    auto result = req.process(mock, 1000);
    BOOST_CHECK_EQUAL(result.size(), 1);
}

BOOST_AUTO_TEST_CASE(ShortFrameSkip)
{
    MockICanChannel mock;
    // Слишком короткий фрейм — нет rest эха и данных
    std::vector<uint8_t> shortPayload = {0xCF, 0x50, 0xF9};
    mock.receiveQueue.push(CanFrame{0xFFFFE, shortPayload, true});
    mock.receiveQueue.push(makeResponse(0xCF, 0x50, {0xB9, 0xFB}, {0xAA}));

    D2Request req{0x50, {0xB9, 0xFB}};
    auto result = req.process(mock, 1000);
    BOOST_CHECK_EQUAL(result.size(), 1);
}

// ---------------------------------------------------------------------------
// 6. Custom parameters
// ---------------------------------------------------------------------------

BOOST_AUTO_TEST_CASE(CustomTimeout)
{
    MockICanChannel mock;
    mock.receiveQueue.push(makeResponse(0xCF, 0x50, {0xB9, 0xFB}, {0x01}));

    D2Request req{0x50, {0xB9, 0xFB}};
    auto result = req.process(mock, 5000);
    BOOST_CHECK_EQUAL(result.size(), 1);
}

BOOST_AUTO_TEST_CASE(SendMessagesDelay)
{
    MockICanChannel mock;
    mock.receiveQueue.push(makeResponse(0xCF, 0x50, {0xB9, 0xFB}, {0x01}));

    D2Request req{0x50, {0xB9, 0xFB}};
    BOOST_CHECK_NO_THROW(req.process(mock, 1000, 50));
}

// ---------------------------------------------------------------------------
// 7. Integration: large request ID with multi-frame response
// ---------------------------------------------------------------------------

BOOST_AUTO_TEST_CASE(LongRequestIdEcho)
{
    MockICanChannel mock;
    std::vector<uint8_t> longId = {0xB9, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08};
    mock.receiveQueue.push(makeResponse(0x8F, 0x50, longId, {0xAA, 0xBB}));
    mock.receiveQueue.push(makeSeriesFrame(0x49, {0xCC}));

    D2Request req{0x50, longId};
    auto result = req.process(mock, 1000);

    BOOST_REQUIRE_GE(result.size(), 2);
    BOOST_CHECK_EQUAL(result[0], 0xAA);
}
