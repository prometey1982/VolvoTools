#include "common/ChecksumHelper.hpp"

#include <map>

namespace util {

namespace {
uint32_t getValueLE(const std::vector<uint8_t>& data, size_t index)
{
    return ((uint32_t)data[index + 3] << 24)
            + ((uint32_t)data[index + 2] << 16)
            + ((uint32_t)data[index + 1] << 8)
            + (uint32_t)data[index];
}

uint32_t getValueBE(const std::vector<uint8_t>& data, size_t index)
{
    return ((uint32_t)data[index] << 24)
           + ((uint32_t)data[index + 1] << 16)
           + ((uint32_t)data[index + 2] << 8)
           + (uint32_t)data[index + 3];
}

void setValueLE(std::vector<uint8_t>& data, size_t index, uint32_t value)
{
    data[index] = static_cast<uint8_t>(value);
    data[index + 1] = static_cast<uint8_t>(value >> 8);
    data[index + 2] = static_cast<uint8_t>(value >> 16);
    data[index + 3] = static_cast<uint8_t>(value >> 24);
}

void setValueBE(std::vector<uint8_t>& data, size_t index, uint32_t value)
{
    data[index + 4] = static_cast<uint8_t>(value);
    data[index + 3] = static_cast<uint8_t>(value >> 8);
    data[index + 2] = static_cast<uint8_t>(value >> 16);
    data[index + 1] = static_cast<uint8_t>(value >> 24);
}

const std::map<uint32_t, std::vector<std::pair<uint32_t, uint32_t>>> CheckBoundsME7 = {
    { 512 * 1024, { { 0x1F810, 0x1FA00 } } },
    { 1024 * 1024, { { 0x1F810, 0x1FC00 } } },
    { 2048 * 1024, { { 0xA0000, 0xA0360 }, { 0x1C91E0, 0x1C9240 } } },
    };

bool checkOrUpdateME7(std::vector<uint8_t>& data, bool update)
{
    const auto boundsIt{CheckBoundsME7.find(data.size())};
    if(boundsIt == CheckBoundsME7.cend()) {
        return false;
    }
    for(const auto& bound: boundsIt->second) {
        uint32_t buffer_index = bound.first;
        uint32_t max_buffer_index = bound.second;
        do {
            uint32_t start_addr = getValueLE(data, buffer_index);

            // Get the checksum zone end address
            uint32_t end_addr = getValueLE(data, buffer_index + 4);

            if (start_addr >= data.size() || end_addr >= data.size())
                break;

            uint32_t checksum = 0;
            for (uint32_t addr = start_addr; addr < end_addr; addr += 2)
                checksum += ((uint32_t)data[addr + 1] << 8) + (uint32_t)data[addr];

            uint32_t curr_checksum = getValueLE(data, buffer_index + 8);
            uint32_t compliment_curr_checksum = getValueLE(data, buffer_index + 12);

            uint32_t complchecksum = ~checksum;

            if(update) {
                setValueLE(data, buffer_index + 8, checksum);
                setValueLE(data, buffer_index + 12, complchecksum);
            }
            else {
                if (curr_checksum != checksum || compliment_curr_checksum != complchecksum)
                    return false;
            }

            buffer_index += 0x10;
        }
        while(buffer_index < max_buffer_index);
    }
    return true;
}

}

ChecksumHelper::ChecksumHelper(
    const common::CarPlatform carPlatform,
    const uint8_t ecuId,
    const std::string& additionalData)
    : _carPlatform(carPlatform)
    , _ecuId(ecuId)
    , _additionalData(additionalData)
{
}

bool ChecksumHelper::isSupported(const std::vector<uint8_t>& data) const
{
    switch(_carPlatform) {
    case common::CarPlatform::P80:
    case common::CarPlatform::P1:
    case common::CarPlatform::P2:
    case common::CarPlatform::P2_250:
        if(_ecuId == 0x7A) {
            return CheckBoundsME7.find(data.size()) != CheckBoundsME7.cend();
        }
    }
    return false;
}

bool ChecksumHelper::check(std::vector<uint8_t>& data) const
{
    switch(_carPlatform) {
    case common::CarPlatform::P80:
    case common::CarPlatform::P1:
    case common::CarPlatform::P2:
    case common::CarPlatform::P2_250:
        if(_ecuId == 0x7A) {
            return checkOrUpdateME7(data, false);
        }
    }
    return false;
}

void ChecksumHelper::update(std::vector<uint8_t>& data) const
{
    switch(_carPlatform) {
    case common::CarPlatform::P80:
    case common::CarPlatform::P1:
    case common::CarPlatform::P2:
    case common::CarPlatform::P2_250:
        if(_ecuId == 0x7A) {
            checkOrUpdateME7(data, true);
        }
    }
}

}
