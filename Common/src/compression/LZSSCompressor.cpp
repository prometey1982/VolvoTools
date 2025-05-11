#include "common/compression/LZSSCompressor.hpp"

#include <stdexcept>

namespace common {

namespace {

static constexpr size_t WINDOW_SIZE = 4096;    // Размер скользящего окна
static constexpr size_t LOOKAHEAD_BUFFER = 18; // Размер буфера предпросмотра
static constexpr size_t MIN_MATCH_LENGTH = 3;  // Минимальная длина совпадения

// Структура для хранения совпадений
struct Match {
    size_t offset;
    size_t length;
    uint8_t next_char;
};

// Функция поиска наилучшего совпадения в скользящем окне
Match find_best_match(const std::vector<uint8_t>& data, size_t window_start, size_t window_end, size_t lookahead_start, size_t lookahead_end)
{
    Match best_match = {0, 0, data[lookahead_start]};
    size_t max_possible_length = std::min(LOOKAHEAD_BUFFER, lookahead_end - lookahead_start);

    for (size_t i = window_start; i < window_end; ++i) {
        size_t length = 0;
        while (length < max_possible_length &&
               i + length < window_end &&
               lookahead_start + length < lookahead_end &&
               data[i + length] == data[lookahead_start + length]) {
            ++length;
        }

        if (length > best_match.length) {
            best_match.length = length;
            best_match.offset = window_end - i;
            if (lookahead_start + length < lookahead_end) {
                best_match.next_char = data[lookahead_start + length];
            } else {
                best_match.next_char = 0;
            }
        }
    }

    return best_match;
}

}

std::vector<uint8_t> LZSSCompressor::compress(const std::vector<uint8_t>& input)
{
    std::vector<uint8_t> output;
    size_t input_size = input.size();
    size_t pos = 0;

    while (pos < input_size) {
        size_t window_start = (pos > WINDOW_SIZE) ? (pos - WINDOW_SIZE) : 0;
        size_t window_end = pos;
        size_t lookahead_end = std::min(pos + LOOKAHEAD_BUFFER, input_size);

        Match match = find_best_match(input, window_start, window_end, pos, lookahead_end);

        if (match.length >= MIN_MATCH_LENGTH) {
            // Кодируем совпадение как (offset, length)
            uint16_t token = ((match.offset & 0xFFF) << 4) | ((match.length - MIN_MATCH_LENGTH) & 0xF);
            output.push_back(static_cast<uint8_t>((token >> 8) & 0xFF));
            output.push_back(static_cast<uint8_t>(token & 0xFF));
            output.push_back(match.next_char);

            pos += match.length + 1;
        } else {
            // Кодируем как одиночный символ
            output.push_back(0);
            output.push_back(match.next_char);

            pos += 1;
        }
    }

    return output;
}

std::vector<uint8_t> LZSSCompressor::decompress(const std::vector<uint8_t>& input)
{
    std::vector<uint8_t> output;
    size_t pos = 0;
    size_t compressed_size = input.size();

    output.reserve(compressed_size * 2); // Предварительное выделение памяти

    while (pos < compressed_size) {
        if (pos + 1 < compressed_size && input[pos] == 0) {
            // Одиночный символ
            output.push_back(input[pos + 1]);
            pos += 2;
        } else if (pos + 2 < compressed_size) {
            // Совпадение (offset, length)
            uint16_t token = (static_cast<uint16_t>(input[pos]) << 8) | input[pos + 1];
            size_t offset = (token >> 4) & 0xFFF;
            size_t length = (token & 0xF) + MIN_MATCH_LENGTH;
            uint8_t next_char = input[pos + 2];

            if (offset == 0 || offset > output.size()) {
                throw std::runtime_error("Invalid offset in compressed data");
            }

            // Копируем совпадение из предыдущих данных
            size_t start_pos = output.size() - offset;
            size_t end_pos = start_pos + length;

            // Проверяем, нужно ли расширять выходной буфер
            if (end_pos > output.size()) {
                // Обработка случая, когда совпадение выходит за пределы текущих данных
                size_t remaining = end_pos - output.size();
                for (size_t i = 0; i < remaining; ++i) {
                    output.push_back(output[start_pos + i]);
                }
            } else {
                output.insert(output.end(), output.begin() + start_pos, output.begin() + end_pos);
            }

            // Добавляем следующий символ
            if (next_char != 0) {
                output.push_back(next_char);
            }

            pos += 3;
        } else {
            // Одиночный символ в конце
            output.push_back(input[pos]);
            pos += 1;
        }
    }

    return output;
}

} // namespace common
