#include "common/protocols/D2Request.hpp"

#include "common/protocols/D2Error.hpp"
#include "common/CanFrame.hpp"
#include "common/ICanChannel.hpp"
#include "common/Util.hpp"

#define LOG_MODULE_NAME "flasher"
#include <common/LogHelper.hpp>


#include <algorithm>
#include <chrono>
#include <stdexcept>
#include <thread>

namespace {

// Каждый фрейм серии несёт не более 7 байт полезных данных.
constexpr size_t maxResponseSize = 64 * 1024;
constexpr size_t maxFrameCount = maxResponseSize / 7 + 4;

void validateRequestId(const std::vector<uint8_t>& requestId)
{
    if (requestId.empty()) {
        throw std::invalid_argument("D2Request: empty requestId");
    }
}

} // namespace

namespace common {

D2Request::D2Request(uint8_t ecuId, std::vector<uint8_t> data)
    : _message{ ecuId, std::move(data) }
{
    validateRequestId(_message.getRequestId());
}

D2Request::D2Request(D2Message message)
    : _message{ std::move(message) }
{
    validateRequestId(_message.getRequestId());
}

std::vector<uint8_t> D2Request::process(ICanChannel& channel, size_t timeout, size_t sendMessagesDelay) const
{
    const uint8_t ecuId = _message.getEcuId();
    const auto& requestId = _message.getRequestId();
    const size_t requestIdSize = requestId.size();

    for (const auto& frame : _message.getFrames()) {
        if (!channel.send(frame, timeout)) {
            LOG_MODULE(ERROR) << "Failed to send CAN message";
            throw std::runtime_error("Failed to send CAN message");
        }
        if (sendMessagesDelay > 0) {
            std::this_thread::sleep_for(std::chrono::milliseconds(sendMessagesDelay));
        }
    }

    enum class ParseState { WaitFirst, WaitSeries };

    // Весь ответ копится в result как поток данных: frame[1..] первого кадра +
    // data-байты кадров серии. Эхо-префикс удаляется в конце одним erase.
    //   нормальный ответ: [0]=ecuId, [1]=requestId[0]+0x40, [2..requestIdSize]=requestId[1..]
    //                      эхо = requestIdSize + 1 байт, может выходить за первый кадр
    //   ошибка:           [0]=ecuId, [1]=0x7F (маркер), [2]=requestId[0] (эхо сервиса) —
    //                      регион 3 байта, [3]=код ошибки (throw D2Error, без кода → 0)
    ParseState state = ParseState::WaitFirst;
    bool isError = false;
    size_t echoRegionSize = 0;
    bool echoComplete = false;
    // База нумерации кадров серии. Зависит от флага серии (bit4) в первом кадре:
    //   0x08 -> кадры 0x09..0x0F, 0x08 (формат, который отдают ЭБУ при чтении памяти);
    //   0x10 -> кадры 0x11..0x17, 0x10 (формат, которым CEM отдаёт текстовые ответы,
    //           например VIN на B9FB).
    uint8_t seriesBase = 0x08;
    uint8_t expectedSeriesId = 1;
    size_t frameCount = 0;
    std::vector<uint8_t> result;

    while (true) {
        CanFrame response;
        if (!channel.receive(response, static_cast<unsigned long>(timeout))) {
            LOG_MODULE(ERROR) << "Failed to receive response";
            throw std::runtime_error("Failed to receive response");
        }
        if (++frameCount > maxFrameCount) {
            LOG_MODULE(ERROR) << "Too many frames in D2 response";
            throw std::runtime_error("Too many frames in D2 response");
        }
        if (response.data.empty()) {
            LOG_MODULE(ERROR) << "Empty response received:" << dumpArray(response.data);
            continue;
        }

        const uint8_t header = response.data[0];
        // bit4 (0x10) — флаг серии: кадр является частью многофреймового ответа и
        // продолжение следует. Реальные ответы CEM (VIN на B9FB, лог VIDA) выставляют
        // его: первый кадр 0x97 (= first + series + 7 байт), серия 0x11..0x17,0x10.
        // Без флага серии младший ниббл кодирует 8 + длину (как в наших запросах:
        // 0xCB = first+last+3); с флагом серии в первом/последнем кадре — длину напрямую.
        const bool hasSeriesFlag = (header & 0x10) != 0;
        bool endSeries = false;
        size_t frameDataSize = 0;

        if (state == ParseState::WaitFirst) {
            // Классификация первого фрейма: кадр без бита «первого», слишком
            // короткий или с несовпавшим маркером — чужой трафик, пропускаем.
            if (!(header & 0x80) || response.data.size() < 3 ||
                response.data[1] != ecuId ||
                (response.data[2] != 0x7F && response.data[2] != requestId[0] + 0x40)) {
                continue;
            }
            // Заголовок первого фрейма: 0x88..0x8F / 0xC8..0xCF без флага серии,
            // 0x90..0x97 / 0xD0..0xD7 — с флагом.
            if (hasSeriesFlag ? ((header & 0x0F) > 0x07) : ((header & 0x0F) < 0x08)) {
                LOG_MODULE(ERROR) << "Invalid header of first D2 response frame";
                throw std::runtime_error("Invalid header of first D2 response frame");
            }
            isError = (response.data[2] == 0x7F);
            echoRegionSize = isError ? 3 : requestIdSize + 1;
            echoComplete = false;
            seriesBase = hasSeriesFlag ? 0x10 : 0x08;
            expectedSeriesId = 1;
            state = ParseState::WaitSeries;
            // Ответ заканчивается кадром «последний» БЕЗ флага серии: одиночный ответ
            // (0xC8..0xCF) или последний кадр серии (0x48..0x4F). Кадр 0x55
            // (last + series) концом не является — за ним идёт продолжение.
            endSeries = ((header & 0x40) != 0) && !hasSeriesFlag;
            frameDataSize = response.data.size() - 1;
        }
        else if (header & 0x40) {
            if (hasSeriesFlag) {
                // Конец подблока, серия продолжается: 0x50..0x57, длина = младший ниббл.
                frameDataSize = header & 0x0F;
            }
            else {
                // Последний фрейм серии: 0x48..0x4F, длина данных = header - 0x48.
                if (header < 0x48 || header > 0x4F) {
                    LOG_MODULE(ERROR) << "Wrong data length in series";
                    throw std::runtime_error("Wrong data length in series");
                }
                frameDataSize = header - 0x48;
                endSeries = true;
            }
            if (response.data.size() < 1 + frameDataSize) {
                LOG_MODULE(ERROR) << "Wrong data length in series";
                throw std::runtime_error("Wrong data length in series");
            }
        }
        else if ((header & 0x80) != 0) {
            // Повтор первого кадра внутри серии (реальный ответ CEM: после кадра 0x55
            // приходит ещё раз 0x8F с тем же эхом и 0xFF-паддингом). Это дубль уже
            // принятого ответа — пропускаем, не сбрасывая накопленные данные.
            LOG_MODULE(DEBUG) << "Duplicated first frame in D2 response, skipped";
            continue;
        }
        else {
            // Серийный кадр: seriesId обязан идти по порядку в базе, заданной первым
            // кадром: 0x09→0x0A→…→0x0F→0x08→… либо 0x11→0x12→…→0x17→0x10→…
            if (header != static_cast<uint8_t>(seriesBase | expectedSeriesId)) {
                LOG_MODULE(ERROR) << "Unexpected seriesId in D2 response";
                throw std::runtime_error("Unexpected seriesId in D2 response");
            }
            expectedSeriesId = (expectedSeriesId + 1) & 0x07;
            frameDataSize = response.data.size() - 1;
        }

        // Общий шаг: копим данные кадра в поток и валидируем эхо-префикс.
        const size_t before = result.size();
        result.insert(result.end(), response.data.cbegin() + 1,
                      response.data.cbegin() + 1 + frameDataSize);
        if (result.size() > maxResponseSize) {
            LOG_MODULE(ERROR) << "D2 response too large";
            throw std::runtime_error("D2 response too large");
        }
        if (!echoComplete && !isError) {
            const size_t end = std::min(result.size(), echoRegionSize);
            size_t pos = before;
            for (; pos < end; ++pos) {
                const uint8_t expected = (pos == 0) ? ecuId
                                          : (pos == 1) ? requestId[0] + 0x40
                                          : requestId[pos - 1];
                if (result[pos] != expected) {
                    break;
                }
            }
            if (pos < end) {
                // Эхо не совпало — чужой трафик: сброс и ждём новый первый кадр.
                LOG_MODULE(DEBUG) << "D2 response echo mismatch, waiting for new first frame";
                state = ParseState::WaitFirst;
                result.clear();
                continue;
            }
            if (result.size() >= echoRegionSize) {
                echoComplete = true;
            }
        }
        if (isError && result.size() >= echoRegionSize) {
            LOG_MODULE(ERROR) << "D2 respond with error: " << dumpArray(result);
            throw D2Error(result.size() > echoRegionSize ? result[echoRegionSize] : 0);
        }

        if (endSeries) {
            if (!echoComplete) {
                LOG_MODULE(ERROR) << "D2 response ended before requestId echo completed";
                throw std::runtime_error("D2 response ended before requestId echo completed");
            }
            break;
        }
    }

    result.erase(result.begin(), result.begin() + echoRegionSize);
    return result;
}

} // namespace common
