#include "common/KeyGenerators.hpp"

namespace common {

namespace {

uint32_t generateKeyImpl(uint32_t hash, uint32_t input)
{
    for (size_t i = 0; i < 32; ++i)
    {
        const bool is_bit_set = (hash ^ input) & 1;
        input >>= 1;
        hash >>= 1;
        if (is_bit_set)
            hash = (hash | 0x800000) ^ 0x109028;
    }
    return hash;
}

}

uint32_t generateKeyVolvoFord(const std::array<uint8_t, 5>& pin_array, const std::array<uint8_t, 3>& seed_array)
{
    const uint32_t high_part = pin_array[4] << 24 | pin_array[3] << 16 | pin_array[2] << 8 | pin_array[1];
    const uint32_t low_part = pin_array[0] << 24 | seed_array[2] << 16 | seed_array[1] << 8 | seed_array[0];
    unsigned int hash = 0xC541A9;
    hash = generateKeyImpl(hash, low_part);
    hash = generateKeyImpl(hash, high_part);
    uint32_t result = ((hash & 0xF00000) >> 12) | hash & 0xF000 | (uint8_t)(16 * hash)
                      | ((hash & 0xFF0) << 12) | ((hash & 0xF0000) >> 16);
    return result;
}

uint32_t generateKeyVAG(uint32_t seed)
{
    for (uint8_t i = 0; i < 5; i++) {
        if ((seed & 0x80000000) == 0x80000000) {
            seed = (0X5FBD5DBD ^ ((seed << 1) | (seed >> 31)));
        }
        else {
            seed = ((seed << 1) | (seed >> 31));
        }
    }
    return seed;
}

} // namespace common
