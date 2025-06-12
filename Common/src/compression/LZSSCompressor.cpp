#include "common/compression/LZSSCompressor.hpp"

namespace common {

namespace {

static constexpr size_t WINDOW_SIZE = 4096;    // Размер скользящего окна
static constexpr size_t LOOKAHEAD_BUFFER = 18; // Размер буфера предпросмотра
static constexpr size_t MIN_MATCH_LENGTH = 3;  // Минимальная длина совпадения

struct Match {
    size_t offset;
    size_t length;
};

Match find_match(const std::vector<uint8_t>& data, size_t pos) {
    Match best = {0, 0};
    if (pos >= data.size()) return best;

    size_t start = (pos > WINDOW_SIZE) ? pos - WINDOW_SIZE : 0;
    start = std::max(start, (size_t)0);

    for (size_t i = start; i < pos; ++i) {
        size_t len = 0;
        while (len < LOOKAHEAD_BUFFER &&
               pos + len < data.size() &&
               i + len < data.size() &&
               data[i + len] == data[pos + len]) {
            len++;
        }
        if (len > best.length) {
            best = {pos - i, len};
        }
    }
    return best;
}

}

std::vector<uint8_t> LZSSCompressor::compress(const std::vector<uint8_t>& input)
{
    std::vector<uint8_t> output;
    std::vector<uint8_t> flags;
    uint8_t current_flag = 0;
    size_t flag_bit = 0;

    for (size_t pos = 0; pos < input.size();) {
        Match match = find_match(input, pos);

        if (match.length >= MIN_MATCH_LENGTH && match.offset <= WINDOW_SIZE) {
            current_flag |= (1 << (7 - flag_bit));
            uint16_t token = ((match.offset - 1) << 4) | (match.length - MIN_MATCH_LENGTH);
            output.push_back(token >> 8);
            output.push_back(token & 0xFF);
            pos += match.length;
        } else {
            output.push_back(input[pos]);
            pos++;
        }

        flag_bit = (flag_bit + 1) % 8;
        if (flag_bit == 0) {
            flags.push_back(current_flag);
            current_flag = 0;
        }
    }

    if (flag_bit != 0) flags.push_back(current_flag);

    // Добавляем заголовок с размером флагов (big-endian)
    uint16_t flag_size = flags.size();
    std::vector<uint8_t> header = {
        static_cast<uint8_t>((flag_size >> 8) & 0xFF),
        static_cast<uint8_t>(flag_size & 0xFF)
    };
    output.insert(output.begin(), flags.begin(), flags.end());
    output.insert(output.begin(), header.begin(), header.end());

    return output;
}

std::vector<uint8_t> LZSSCompressor::decompress(const std::vector<uint8_t>& input)
{
    if (input.size() < 2) return {};

    // Читаем размер флагов (big-endian)
    size_t flag_bytes = (input[0] << 8) | input[1];
    if (input.size() < 2 + flag_bytes) return {};

    std::vector<uint8_t> flags(input.begin() + 2, input.begin() + 2 + flag_bytes);
    std::vector<uint8_t> output;
    size_t data_pos = 2 + flag_bytes;
    size_t flag_idx = 0;
    size_t bit_idx = 0;

    while (data_pos < input.size() && flag_idx < flags.size()) {
        bool is_compressed = (flags[flag_idx] & (1 << (7 - bit_idx)));

        if (is_compressed) {
            if (data_pos + 1 >= input.size()) break;

            uint16_t token = (input[data_pos] << 8) | input[data_pos + 1];
            size_t offset = (token >> 4) + 1;
            size_t length = (token & 0x0F) + MIN_MATCH_LENGTH;

            size_t copy_pos = output.size() - std::min(offset, output.size());
            for (size_t i = 0; i < length; ++i) {
                output.push_back(output[copy_pos + (i % offset)]);
            }

            data_pos += 2;
        } else {
            output.push_back(input[data_pos]);
            data_pos++;
        }

        bit_idx = (bit_idx + 1) % 8;
        if (bit_idx == 0) flag_idx++;
    }

    return output;
}

} // namespace common
