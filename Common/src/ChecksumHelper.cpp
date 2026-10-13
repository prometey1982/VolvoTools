#include "common/ChecksumHelper.hpp"

#include "common/Crc.hpp"

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
    data[index + 0] = static_cast<uint8_t>(value);
    data[index + 1] = static_cast<uint8_t>(value >> 8);
    data[index + 2] = static_cast<uint8_t>(value >> 16);
    data[index + 3] = static_cast<uint8_t>(value >> 24);
}

void setValueBE(std::vector<uint8_t>& data, size_t index, uint32_t value)
{
    data[index + 3] = static_cast<uint8_t>(value);
    data[index + 2] = static_cast<uint8_t>(value >> 8);
    data[index + 1] = static_cast<uint8_t>(value >> 16);
    data[index + 0] = static_cast<uint8_t>(value >> 24);
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

bool checkOrUpdateDensoECMImpl(
    std::vector<uint8_t>& data, const size_t checksumAddr, bool update)
{
    constexpr size_t blocksCount = 16;
    constexpr size_t recordSize = 12;
    constexpr uint32_t densoMagic = 0x5AA5A55A;
    for(size_t i = 0; i < blocksCount; ++i) {
        const size_t baseAddr = checksumAddr + i * recordSize;
        const uint32_t startAddr = getValueBE(data, baseAddr);
        const uint32_t endAddr = getValueBE(data, baseAddr + 4);
        const uint32_t checksum = getValueBE(data, baseAddr + 8);
        uint32_t calculatedChecksum = 0;
        for(size_t addr = startAddr; addr < endAddr; addr += 4) {
            calculatedChecksum += getValueBE(data, addr);
        }
        calculatedChecksum = densoMagic - calculatedChecksum;
        if(update) {
            setValueBE(data, baseAddr + 8, calculatedChecksum);
        }
        else {
            if(calculatedChecksum != checksum) {
                return false;
            }
        }
    }
    return true;
}

bool checkOrUpdateDensoECM2MB(std::vector<uint8_t>& data, bool update)
{
    constexpr size_t checksumAddr = 0x1FFC00;
    return checkOrUpdateDensoECMImpl(data, checksumAddr, update);
}

bool checkOrUpdateDensoECM1MB(std::vector<uint8_t>& data, bool update)
{
    constexpr size_t checksumAddr = 0xFFF00;
    return checkOrUpdateDensoECMImpl(data, checksumAddr, update);
}

bool checkOrUpdateTF80P2(std::vector<uint8_t>& data, bool update)
{
    // checksum_array_7FFF0: {u32 count; u32 desc[count]; u32 0xFFFFFFFF}
    // Прошивка обходит блоки по count: 0xA5A4 "mov.l @r2,r6" (checksum_check),
    // 0xA6A0 "mov.l @r1,r3" (checksum_step_crc_A688), 0xA67E (checksum_reset_A666).
    // Терминатор 0xFFFFFFFF код не читает, поэтому на него не опираемся.
    constexpr size_t tableAddr = 0x7FFF0;
    constexpr size_t descSize = 0x10;
    constexpr uint32_t maxBlocksCount = 8;
    if(data.size() < tableAddr + 8) {
        return false;
    }
    const uint32_t blocksCount = getValueBE(data, tableAddr);
    if(blocksCount < 1 || blocksCount > maxBlocksCount) {
        return false;
    }
    if(tableAddr + 4 + static_cast<size_t>(blocksCount) * 4 > data.size()) {
        return false;
    }
    for(size_t i = 0; i < blocksCount; ++i) {
        const size_t blockAddr = getValueBE(data, tableAddr + 4 + i * 4);
        if(blockAddr > data.size() - descSize) {
            return false;
        }
        const uint32_t startAddr = getValueBE(data, blockAddr + 4);
        const uint32_t endAddr = getValueBE(data, blockAddr + 8);
        const size_t crcAddr = blockAddr + 12;
        // end — последний байт диапазона включительно (док CHECKSUM.md §3.2, §6)
        if(endAddr >= data.size() || endAddr <= startAddr) {
            return false;
        }
        // CRC-поле обязано лежать вне своего диапазона (§7), иначе пересчёт сам себя обесценит
        if(startAddr <= crcAddr + 3 && crcAddr <= endAddr) {
            return false;
        }
        const uint16_t calculatedChecksum =
            common::crc16XModemLE(data.data() + startAddr, endAddr - startAddr + 1);
        if(update) {
            setValueBE(data, crcAddr, calculatedChecksum);
        }
        else if(calculatedChecksum != (getValueBE(data, crcAddr) & 0xFFFF)) {
            return false;
        }
    }
    return true;
}

bool checkOrUpdateImpl(
    common::CarPlatform carPlatform, uint8_t ecuId, const std::string& additionalData,
    std::vector<uint8_t>& data, bool update)
{
    switch(carPlatform) {
    case common::CarPlatform::P80:
    case common::CarPlatform::P1:
    case common::CarPlatform::P2:
    case common::CarPlatform::P2_250:
        if(ecuId == 0x7A) {
            return checkOrUpdateME7(data, update);
        }
        else if(ecuId == 0x6E && additionalData == "tf80_p2") {
            return checkOrUpdateTF80P2(data, update);
        }
        break;
    case common::CarPlatform::P3:
        if(ecuId == 0x10) {
            if(additionalData == "denso_p3_restyling") {
                return checkOrUpdateDensoECM2MB(data, update);
            }
            else if(additionalData == "denso_p3") {
                return checkOrUpdateDensoECM1MB(data, update);
            }
        }
        break;
    }
    return false;
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
        else if(_ecuId == 0x6E && _additionalData == "tf80_p2") {
            return true;
        }
        break;
    case common::CarPlatform::P3:
        if(_ecuId == 0x10 && (_additionalData == "denso_p3_restyling" || _additionalData == "denso_p3")) {
            return true;
        }
        break;
    }
    return false;
}

bool ChecksumHelper::check(std::vector<uint8_t>& data) const
{
    return checkOrUpdateImpl(_carPlatform, _ecuId, _additionalData, data, false);
}

void ChecksumHelper::update(std::vector<uint8_t>& data) const
{
    checkOrUpdateImpl(_carPlatform, _ecuId, _additionalData, data, true);
}

}
