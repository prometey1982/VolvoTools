#pragma once

#include <array>

namespace common {

uint32_t generateKeyVolvoFord(const std::array<uint8_t, 5>& pin_array, const std::array<uint8_t, 3>& seed_array);
uint32_t generateKeyVolvoHaldex23(const std::array<uint8_t, 5>& pin_array, const std::array<uint8_t, 3>& seed_array);
uint32_t generateKeyVAG(uint32_t seed);

enum class HavalKeyType {
    TCM_02,
    TCM_03,
    ECM_03,
};
uint32_t generateKeyHaval(uint32_t seed, HavalKeyType keyType);

} // namespace common
