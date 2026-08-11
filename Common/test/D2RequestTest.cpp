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

// Формат ошибки (framed): [0]=header, [1]=ecuId, [2]=0x7F,
// [3..2+size]=полное эхо requestId, [3+size]=код ошибки.
static CanFrame makeErrorResponse(uint8_t ecuId,
                                  const std::vector<uint8_t>& requestId,
                                  uint8_t errorCode)
{
    std::vector<uint8_t> payload;
    payload.push_back(0xCF);
    payload.push_back(ecuId);
    payload.push_back(0x7F);
    payload.insert(payload.end(), requestId.begin(), requestId.end());
    payload.push_back(errorCode);
    return {0xFFFFE, std::move(payload), true};
}

// Разбивает поток данных ответа (без header-байтов) на реальные CAN-фреймы
// (≤ 8 байт) с корректными префиксами и seriesId — эхо requestId может
// выходить за первый кадр.
static std::vector<CanFrame> makeFramedStream(const std::vector<uint8_t>& stream)
{
    std::vector<CanFrame> frames;
    constexpr size_t maxData = 7;
    if (stream.empty()) {
        return frames;
    }
    const bool single = stream.size() <= maxData;
    const size_t firstN = std::min(maxData, stream.size());
    std::vector<uint8_t> payload(8, 0);
    payload[0] = single ? static_cast<uint8_t>(0xC8 + firstN) : static_cast<uint8_t>(0x88 + firstN);
    std::copy(stream.begin(), stream.begin() + firstN, payload.begin() + 1);
    frames.emplace_back(0xFFFFE, std::move(payload), true);

    uint8_t seriesId = 0x09;
    for (size_t pos = firstN; pos < stream.size();) {
        const size_t n = std::min(maxData, stream.size() - pos);
        const bool last = (pos + n >= stream.size());
        payload.assign(8, 0);
        payload[0] = last ? static_cast<uint8_t>(0x48 + n) : seriesId;
        std::copy(stream.begin() + pos, stream.begin() + pos + n, payload.begin() + 1);
        frames.emplace_back(0xFFFFE, std::move(payload), true);
        pos += n;
        seriesId = 0x08 + ((seriesId - 0x08 + 1) & 0x07);
    }
    return frames;
}

// Собирает многофреймовый ответ с эхом requestId (нормальный формат).
static std::vector<CanFrame> makeFramedResponse(uint8_t ecuId,
                                                const std::vector<uint8_t>& requestId,
                                                const std::vector<uint8_t>& data)
{
    std::vector<uint8_t> stream;
    stream.push_back(ecuId);
    stream.push_back(requestId[0] + 0x40);
    stream.insert(stream.end(), requestId.begin() + 1, requestId.end());
    stream.insert(stream.end(), data.begin(), data.end());
    return makeFramedStream(stream);
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
    mock.receiveQueue.push(makeSeriesFrame(0x4A, {0x05, 0x06}));

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
// 3. SeriesId sequence (0x09→0x0A→…→0x0F→0x08→…)
// ---------------------------------------------------------------------------

BOOST_AUTO_TEST_CASE(MultiFrameSeriesWrapAround)
{
    MockICanChannel mock;
    mock.receiveQueue.push(makeResponse(0x8F, 0x50, {0xB9, 0xFB}, {0x00}));

    std::vector<uint8_t> payload;
    payload.push_back(0x00);
    uint8_t seriesId = 0x09;
    for (int i = 0; i < 12; ++i) {
        const std::vector<uint8_t> data(7, static_cast<uint8_t>(0x10 + i));
        mock.receiveQueue.push(makeSeriesFrame(seriesId, data));
        payload.insert(payload.end(), data.begin(), data.end());
        seriesId = 0x08 + ((seriesId - 0x08 + 1) & 0x07);
    }
    mock.receiveQueue.push(makeSeriesFrame(0x49, {0xFE}));
    payload.push_back(0xFE);

    D2Request req{0x50, {0xB9, 0xFB}};
    auto result = req.process(mock, 1000);

    BOOST_REQUIRE_EQUAL(result.size(), payload.size());
    BOOST_CHECK_EQUAL_COLLECTIONS(result.begin(), result.end(), payload.begin(), payload.end());
}

BOOST_AUTO_TEST_CASE(SeriesIdMissingFrame)
{
    MockICanChannel mock;
    mock.receiveQueue.push(makeResponse(0x8F, 0x50, {0xB9, 0xFB}, {0x01, 0x02, 0x03, 0x04}));
    // Ожидается 0x09, пришёл 0x0A — потерян кадр серии.
    mock.receiveQueue.push(makeSeriesFrame(0x0A, {0x05, 0x06, 0x07}));

    D2Request req{0x50, {0xB9, 0xFB}};
    BOOST_CHECK_THROW(req.process(mock, 1000), std::runtime_error);
}

BOOST_AUTO_TEST_CASE(SeriesIdDuplicateFrame)
{
    MockICanChannel mock;
    mock.receiveQueue.push(makeResponse(0x8F, 0x50, {0xB9, 0xFB}, {0x01, 0x02, 0x03, 0x04}));
    mock.receiveQueue.push(makeSeriesFrame(0x09, {0x05, 0x06, 0x07}));
    // Ожидается 0x0A, пришёл повторный 0x09 — дубликат.
    mock.receiveQueue.push(makeSeriesFrame(0x09, {0x08, 0x09, 0x0A}));

    D2Request req{0x50, {0xB9, 0xFB}};
    BOOST_CHECK_THROW(req.process(mock, 1000), std::runtime_error);
}

// ---------------------------------------------------------------------------
// 4. Limits: response too large / too many frames
// ---------------------------------------------------------------------------

BOOST_AUTO_TEST_CASE(SeriesWithoutEndLimit)
{
    MockICanChannel mock;
    mock.receiveQueue.push(makeResponse(0x8F, 0x50, {0xB9, 0xFB}, {0x01, 0x02, 0x03, 0x04}));
    uint8_t seriesId = 0x09;
    for (size_t i = 0; i < 10000; ++i) {
        const std::vector<uint8_t> data(7, static_cast<uint8_t>(i & 0xFF));
        mock.receiveQueue.push(makeSeriesFrame(seriesId, data));
        seriesId = 0x08 + ((seriesId - 0x08 + 1) & 0x07);
    }

    D2Request req{0x50, {0xB9, 0xFB}};
    BOOST_CHECK_THROW(req.process(mock, 1000), std::runtime_error);
}

// ---------------------------------------------------------------------------
// 5. Echo check failures (skip foreign traffic)
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
    // Ответ с неправильным requestId[0]+0x40 (0x00 вместо 0xF9)
    std::vector<uint8_t> payload1 = {0xCF, 0x50, 0x00, 0xFB, 0x01};
    mock.receiveQueue.push(CanFrame{0xFFFFE, payload1, true});
    mock.receiveQueue.push(makeResponse(0xCF, 0x50, {0xB9, 0xFB}, {0xAA}));

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
// 6. Frame filtering: non-first frames, invalid first header, response CAN ID
// ---------------------------------------------------------------------------

BOOST_AUTO_TEST_CASE(DifferentResponseIdAccepted)
{
    MockICanChannel mock;
    // Реальные ответы D2 приходят с CAN ID ≠ 0xFFFFE — проверки ID нет,
    // фильтрация только протокольная (маркер/эхо).
    std::vector<uint8_t> responsePayload = {0xCF, 0x50, 0xF9, 0xFB, 0x99};
    mock.receiveQueue.push(CanFrame{0x123, responsePayload, true});

    D2Request req{0x50, {0xB9, 0xFB}};
    auto result = req.process(mock, 1000);
    BOOST_REQUIRE_EQUAL(result.size(), 1);
    BOOST_CHECK_EQUAL(result[0], 0x99);
}

BOOST_AUTO_TEST_CASE(NonFirstFrameSkip)
{
    MockICanChannel mock;
    // Серийный кадр без бита 0x80 — даже с совпадающими байтами эха пропускается.
    mock.receiveQueue.push(makeSeriesFrame(0x09, {0x50, 0xF9, 0xFB, 0x99}));
    mock.receiveQueue.push(makeResponse(0xCF, 0x50, {0xB9, 0xFB}, {0xAA}));

    D2Request req{0x50, {0xB9, 0xFB}};
    auto result = req.process(mock, 1000);
    BOOST_REQUIRE_EQUAL(result.size(), 1);
    BOOST_CHECK_EQUAL(result[0], 0xAA);
}

BOOST_AUTO_TEST_CASE(InvalidFirstHeaderAfterEcho)
{
    MockICanChannel mock;
    // Эхо совпало, но header 0x80 вне допустимых диапазонов 0x88..0x8F / 0xC8..0xCF.
    std::vector<uint8_t> badPayload = {0x80, 0x50, 0xF9, 0xFB, 0xAA};
    mock.receiveQueue.push(CanFrame{0xFFFFE, badPayload, true});

    D2Request req{0x50, {0xB9, 0xFB}};
    BOOST_CHECK_THROW(req.process(mock, 1000), std::runtime_error);
}

// ---------------------------------------------------------------------------
// 7. Series framing violations
// ---------------------------------------------------------------------------

BOOST_AUTO_TEST_CASE(LastFrameHeaderBelowRange)
{
    MockICanChannel mock;
    mock.receiveQueue.push(makeResponse(0x8F, 0x50, {0xB9, 0xFB}, {0x01, 0x02, 0x03, 0x04}));
    mock.receiveQueue.push(makeSeriesFrame(0x40, {0x05, 0x06}));

    D2Request req{0x50, {0xB9, 0xFB}};
    BOOST_CHECK_THROW(req.process(mock, 1000), std::runtime_error);
}

BOOST_AUTO_TEST_CASE(LastFrameHeaderAboveRange)
{
    MockICanChannel mock;
    mock.receiveQueue.push(makeResponse(0x8F, 0x50, {0xB9, 0xFB}, {0x01, 0x02, 0x03, 0x04}));
    mock.receiveQueue.push(makeSeriesFrame(0x50, {0x05, 0x06}));

    D2Request req{0x50, {0xB9, 0xFB}};
    BOOST_CHECK_THROW(req.process(mock, 1000), std::runtime_error);
}

BOOST_AUTO_TEST_CASE(LastFrameShorterThanDeclared)
{
    MockICanChannel mock;
    mock.receiveQueue.push(makeResponse(0x8F, 0x50, {0xB9, 0xFB}, {0x01, 0x02, 0x03, 0x04}));
    // Заявлена длина 2 (0x4A), а данных только 1.
    mock.receiveQueue.push(makeSeriesFrame(0x4A, {0x05}));

    D2Request req{0x50, {0xB9, 0xFB}};
    BOOST_CHECK_THROW(req.process(mock, 1000), std::runtime_error);
}

// ---------------------------------------------------------------------------
// 8. Error handling
// ---------------------------------------------------------------------------

BOOST_AUTO_TEST_CASE(ErrorResponse)
{
    MockICanChannel mock;
    mock.receiveQueue.push(makeErrorResponse(0x50, {0xB9, 0xFB}, 0x22));

    D2Request req{0x50, {0xB9, 0xFB}};
    BOOST_CHECK_THROW(req.process(mock, 1000), D2Error);
}

BOOST_AUTO_TEST_CASE(ErrorWrongEcuIdInError)
{
    MockICanChannel mock;
    mock.receiveQueue.push(makeErrorResponse(0xFF, {0xB9, 0xFB}, 0x22));
    mock.receiveQueue.push(makeResponse(0xCF, 0x50, {0xB9, 0xFB}, {0xAA}));

    D2Request req{0x50, {0xB9, 0xFB}};
    BOOST_CHECK_NO_THROW(req.process(mock, 1000));
}

BOOST_AUTO_TEST_CASE(ErrorResponseCorrectCode)
{
    MockICanChannel mock;
    mock.receiveQueue.push(makeErrorResponse(0x50, {0xB9, 0xFB}, 0x78));

    D2Request req{0x50, {0xB9, 0xFB}};
    try {
        req.process(mock, 1000);
        BOOST_FAIL("Expected D2Error");
    } catch (const D2Error& e) {
        BOOST_CHECK_EQUAL(e.getErrorCode(), 0x78);
    }
}

// ---------------------------------------------------------------------------
// 9. Timeouts and empty frames
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
    // Слишком короткий фрейм (< 3 байт) — нельзя проверить маркер, пропускается
    std::vector<uint8_t> shortPayload = {0xCF, 0x50};
    mock.receiveQueue.push(CanFrame{0xFFFFE, shortPayload, true});
    mock.receiveQueue.push(makeResponse(0xCF, 0x50, {0xB9, 0xFB}, {0xAA}));

    D2Request req{0x50, {0xB9, 0xFB}};
    auto result = req.process(mock, 1000);
    BOOST_CHECK_EQUAL(result.size(), 1);
}

// ---------------------------------------------------------------------------
// 10. RequestId validation
// ---------------------------------------------------------------------------

BOOST_AUTO_TEST_CASE(EmptyRequestIdThrows)
{
    BOOST_CHECK_THROW((D2Request{0x50, {}}), std::invalid_argument);
}

BOOST_AUTO_TEST_CASE(FiveByteRequestIdWorks)
{
    MockICanChannel mock;
    std::vector<uint8_t> maxId = {0xB9, 0x01, 0x02, 0x03, 0x04};
    mock.receiveQueue.push(makeResponse(0x8F, 0x50, maxId, {0xAA, 0xBB}));
    mock.receiveQueue.push(makeSeriesFrame(0x49, {0xCC}));

    D2Request req{0x50, maxId};
    auto result = req.process(mock, 1000);

    BOOST_REQUIRE_GE(result.size(), 2);
    BOOST_CHECK_EQUAL(result[0], 0xAA);
}

// ---------------------------------------------------------------------------
// 11. RequestId echo spanning multiple CAN frames
// ---------------------------------------------------------------------------

BOOST_AUTO_TEST_CASE(LongRequestIdEchoSpansFrames)
{
    MockICanChannel mock;
    const std::vector<uint8_t> requestId = {0xB9, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08};
    std::vector<uint8_t> payload;
    for (int i = 0; i < 20; ++i) {
        payload.push_back(static_cast<uint8_t>(0x30 + i));
    }
    for (auto& frame : makeFramedResponse(0x50, requestId, payload)) {
        mock.receiveQueue.push(std::move(frame));
    }

    D2Request req{0x50, requestId};
    auto result = req.process(mock, 1000);

    BOOST_REQUIRE_EQUAL(result.size(), payload.size());
    BOOST_CHECK_EQUAL_COLLECTIONS(result.begin(), result.end(), payload.begin(), payload.end());
}

BOOST_AUTO_TEST_CASE(LongRequestIdEchoBoundary)
{
    MockICanChannel mock;
    // Эхо заканчивается ровно на границе кадра (7 байт эха в первом кадре).
    const std::vector<uint8_t> requestId = {0xB9, 0x01, 0x02, 0x03, 0x04, 0x05};  // эхо = 6 байт
    std::vector<uint8_t> payload = {0xAA, 0xBB, 0xCC};
    for (auto& frame : makeFramedResponse(0x50, requestId, payload)) {
        mock.receiveQueue.push(std::move(frame));
    }

    D2Request req{0x50, requestId};
    auto result = req.process(mock, 1000);

    BOOST_REQUIRE_EQUAL(result.size(), payload.size());
    BOOST_CHECK_EQUAL_COLLECTIONS(result.begin(), result.end(), payload.begin(), payload.end());
}

BOOST_AUTO_TEST_CASE(ErrorResponseLongRequestId)
{
    MockICanChannel mock;
    const std::vector<uint8_t> requestId = {0xB9, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08};
    std::vector<uint8_t> stream;
    stream.push_back(0x50);
    stream.push_back(0x7F);
    stream.insert(stream.end(), requestId.begin(), requestId.end());
    stream.push_back(0x22);
    for (auto& frame : makeFramedStream(stream)) {
        mock.receiveQueue.push(std::move(frame));
    }

    D2Request req{0x50, requestId};
    try {
        req.process(mock, 1000);
        BOOST_FAIL("Expected D2Error");
    } catch (const D2Error& e) {
        BOOST_CHECK_EQUAL(e.getErrorCode(), 0x22);
    }
}

BOOST_AUTO_TEST_CASE(SingleFrameResponseShortEchoThrows)
{
    MockICanChannel mock;
    // single-frame ответ не может вместить полное эхо длинного requestId.
    std::vector<uint8_t> shortFrame = {0xCF, 0x50, 0xF9, 0x01, 0x02};
    mock.receiveQueue.push(CanFrame{0xFFFFE, shortFrame, true});

    D2Request req{0x50, {0xB9, 0x01, 0x02, 0x03, 0x04}};
    BOOST_CHECK_THROW(req.process(mock, 1000), std::runtime_error);
}

BOOST_AUTO_TEST_CASE(SeriesEndsBeforeEchoComplete)
{
    MockICanChannel mock;
    const std::vector<uint8_t> requestId = {0xB9, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08};
    // Поток обрывается до завершения эха: 7 байт в первом кадре + 1 в последнем < 10.
    std::vector<uint8_t> stream = {0x50, 0xF9, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06};
    for (auto& frame : makeFramedStream(stream)) {
        mock.receiveQueue.push(std::move(frame));
    }

    D2Request req{0x50, requestId};
    BOOST_CHECK_THROW(req.process(mock, 1000), std::runtime_error);
}

// ---------------------------------------------------------------------------
// 12. Custom parameters
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
