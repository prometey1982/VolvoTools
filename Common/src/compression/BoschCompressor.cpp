#pragma once

#include "common/compression/BoschCompressor.hpp"

namespace common {

namespace {

static constexpr uint16_t MAX_BLOCK_SIZE = 0x3FFF;    // Максимальный размер блока
static constexpr uint16_t SAFE_BLOCK_SIZE = 0x3FF0;    // Безопасный размер блока
static constexpr uint16_t RLE_THRESHOLD = 5;          // Минимум повторений для RLE
static constexpr uint8_t BOSCH_HEADER1 = 0x1A;        // Первый байт заголовка
static constexpr uint8_t BOSCH_HEADER2 = 0x01;        // Второй байт заголовка
static constexpr uint8_t CHECKSUM_MARKER = 0xC0;      // Маркер начала контрольной суммы
static constexpr uint16_t RLE_COMMAND_FLAG = 0x4000;  // Флаг RLE-команды

static size_t countRepeats(const std::vector<uint8_t>& input, size_t pos)
{
    if(pos >= input.size())
        return 0;

    const uint8_t value = input[pos];
    size_t count = 1;

    while (pos + count < input.size() &&
           input[pos + count] == value &&
           count < MAX_BLOCK_SIZE) {  // Используем константу
        count++;
    }

    return count;
}

static void writeRawBlock(std::vector<uint8_t>& output,
                          const std::vector<uint8_t>& input,
                          size_t current_pos,
                          size_t& bytes_not_touched)
{
    if (bytes_not_touched == 0) return;

    output.push_back(static_cast<uint8_t>((bytes_not_touched >> 8) & 0xFF));
    output.push_back(static_cast<uint8_t>(bytes_not_touched & 0xFF));

    output.insert(output.end(),
                  input.begin() + current_pos - bytes_not_touched,
                  input.begin() + current_pos);

    bytes_not_touched = 0;
}

static void writeRleBlock(std::vector<uint8_t>& output, uint8_t value, size_t count)
{
    uint16_t command = RLE_COMMAND_FLAG | static_cast<uint16_t>(count);
    output.push_back(static_cast<uint8_t>((command >> 8) & 0xFF));
    output.push_back(static_cast<uint8_t>(command & 0xFF));
    output.push_back(value);
}

static void writeChecksum(std::vector<uint8_t>& output, const std::vector<uint8_t>& input)
{
    uint32_t checksum = 0;
    for (uint8_t byte : input) {
        checksum += byte;
    }

    output.push_back(CHECKSUM_MARKER);
    output.push_back(0x00); // Первый байт суммы обнулен
    output.push_back(static_cast<uint8_t>((checksum >> 16) & 0xFF));
    output.push_back(static_cast<uint8_t>((checksum >> 8) & 0xFF));
    output.push_back(static_cast<uint8_t>(checksum & 0xFF));
}
}

std::vector<uint8_t> BoschCompressor::compress(const std::vector<uint8_t>& input)
{
    std::vector<uint8_t> output;
    output.reserve(input.size()); // Оптимизация: резервируем память заранее

    // Заголовок
    output.push_back(BOSCH_HEADER1);
    output.push_back(BOSCH_HEADER2);

    size_t i = 0;
    const size_t input_size = input.size();
    size_t bytes_not_touched = 0;

    while (i < input_size) {
        // Проверяем лимит для неповторяющихся байтов
        if (bytes_not_touched >= SAFE_BLOCK_SIZE) {
            writeRawBlock(output, input, i, bytes_not_touched);
        }

        // Ищем последовательность одинаковых байтов
        size_t repeat_count = countRepeats(input, i);

        if (repeat_count >= RLE_THRESHOLD) {
            // Записываем накопленные неповторяющиеся байты
            if (bytes_not_touched > 0) {
                writeRawBlock(output, input, i, bytes_not_touched);
            }

            // Записываем RLE-блок (но не превышаем максимальный размер)
            size_t actual_repeats = std::min(repeat_count, static_cast<size_t>(MAX_BLOCK_SIZE));
            writeRleBlock(output, input[i], actual_repeats);
            i += actual_repeats;
        } else {
            bytes_not_touched++;
            i++;
        }
    }

    // Записываем оставшиеся байты
    if (bytes_not_touched > 0) {
        writeRawBlock(output, input, i, bytes_not_touched);
    }

    // Контрольная сумма
    writeChecksum(output, input);

    return output;
}

std::vector<uint8_t> BoschCompressor::decompress(const std::vector<uint8_t>& input)
{
    if (input.size() < 7 || input[0] != BOSCH_HEADER1 || input[1] != BOSCH_HEADER2) {
        return {};
    }

    std::vector<uint8_t> output;
    output.reserve(input.size() * 2); // Эвристика для резервирования памяти

    for (size_t i = 2; i < input.size() - 5;) {
        if (i + 1 >= input.size()) break;

        uint16_t command = (input[i] << 8) | input[i + 1];
        i += 2;

        if (command & RLE_COMMAND_FLAG) { // RLE-блок
            uint16_t count = command & MAX_BLOCK_SIZE;
            if (i >= input.size()) break;

            uint8_t value = input[i++];
            output.insert(output.end(), count, value);
        } else { // Raw-блок
            if (i + command > input.size()) break;

            output.insert(output.end(), input.begin() + i, input.begin() + i + command);
            i += command;
        }
    }

    return output;
}

} // namespace common
