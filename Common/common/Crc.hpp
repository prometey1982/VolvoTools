#pragma once

#include <cinttypes>

namespace common {

uint16_t crc16(const uint8_t* data_p, size_t length);
uint16_t crc16XModemLE(const uint8_t* data_p, size_t length);
uint16_t crc16XModemBE(const uint8_t* data_p, size_t length);

} // namespace common
