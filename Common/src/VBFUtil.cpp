#include "common/VBFUtil.hpp"

#include "common/ChecksumHelper.hpp"
#include "common/VBFParser.hpp"
#include "common/Util.hpp"

#include <intelhex.h>

#include <stdexcept>

namespace common {

namespace {

void updateChecksum(std::vector<uint8_t>& data)
{
    util::ChecksumHelper helper;
    for(size_t i = 0; i < 5 && !helper.check(data); ++i) {
        helper.update(data);
    }
    if(!helper.check(data)) {
        throw std::runtime_error("Failed to check and update checksum");
    }
}

VBFChunk createChunk(const std::vector<uint8_t>& data, uint32_t offset, size_t size)
{
    if(offset + size > data.size()) {
        throw std::runtime_error("Flash file too small");
    }
    return {offset, std::vector<uint8_t>(data.data() + offset, data.data() + offset + size), crc16(data.data() + offset, size)};
}

VBF createVBFForME7(std::vector<uint8_t>& data)
{
    updateChecksum(data);
    return {{}, {createChunk(data, 0x8000, 0x6000),
                 createChunk(data, 0x10000, data.size() - 0x10000)}};
}

VBF createVBFForME9P1(std::vector<uint8_t>& data)
{
    updateChecksum(data);
    return {{}, {createChunk(data, 0x20000, 0x70000),
                 createChunk(data, 0xA0000, data.size() - 0xA0000)}};
}

VBF createVBFForME9P3(std::vector<uint8_t>& data)
{
    // TODO: need to detect checksum areas correctly.
    //updateChecksum(data);
    return {{}, {createChunk(data, 0x20000, 0x70000),
                 createChunk(data, 0xA0000, 0x120000),
                 createChunk(data, 0x1C2000, 0x1E000),
                 createChunk(data, 0x1E0000, 0x20000)
                }};
}

VBF createVBFForVAGMED91(std::vector<uint8_t>& data)
{
    return { {}, {createChunk(data, 0x20000, 0x60000),
                  createChunk(data, 0xA0000, 0x120000),
                  createChunk(data, 0x1F0000, 0x10000),
                  createChunk(data, 0x80000, 0x20000),
                  createChunk(data, 0x1C0000, 0x30000)} };
}

VBF createVBFForVAGMED912(std::vector<uint8_t>& data)
{
    updateChecksum(data);
    return { {}, {createChunk(data, 0x20000, 0x70000),
                 createChunk(data, 0xA0000, data.size() - 0xA0000)} };
}

VBF createVBFForTCM(std::vector<uint8_t>& data)
{
    return {{}, {createChunk(data, 0x8000, 0x8000),
                 createChunk(data, 0x10000, 0x10000),
                 createChunk(data, 0x20000, 0x10000),
                 createChunk(data, 0x30000, 0x10000),
                 createChunk(data, 0x40000, 0x10000),
                 createChunk(data, 0x50000, 0x10000),
                 createChunk(data, 0x60000, 0x10000),
                 createChunk(data, 0x70000, 0x10000)}};
}

VBF createVbfFromBinary(CarPlatform carPlatform, uint8_t ecuId,
                                const std::string& additionalData, std::vector<uint8_t>&& data)
{
    switch(carPlatform) {
    case CarPlatform::P80:
    case CarPlatform::P2:
    case CarPlatform::P2_250:
        if(ecuId == 0x7A) {
            return createVBFForME7(data);
        }
        else if(ecuId == 0x6E) {
            return createVBFForTCM(data);
        }
        break;
    case common::CarPlatform::P1:
        if(ecuId == 0x7A) {
            return createVBFForME9P1(data);
        }
        else if(ecuId == 0x6E) {
            return createVBFForTCM(data);
        }
        break;
    case common::CarPlatform::P3:
    case common::CarPlatform::Ford_UDS:
        if(ecuId == 0x10 && toLower(additionalData) == "me9_p3") {
            return createVBFForME9P3(data);
        }
        else if(ecuId == 0x18) {
            return createVBFForTCM(data);
        }
        break;
    case common::CarPlatform::VAG_MED91:
        return createVBFForVAGMED91(data);
    case common::CarPlatform::VAG_MED912:
        return createVBFForVAGMED912(data);
    default:
        break;
    }
    throw std::runtime_error("Unsupported ECU to load from binary");
}

}

common::VBF loadVBFForFlasher(CarPlatform carPlatform, uint8_t ecuId,
                              const std::string& additionalData, const std::string& path,
                              std::istream& input)
{
    if(path.rfind(".vbf") != std::string::npos) {
        common::VBFParser parser;
        return parser.parse(input);
    }
    if(path.rfind(".hex") != std::string::npos) {
        intelhex::hex_data hexData;
        hexData.read(input);
        hexData.compact();
        std::vector<common::VBFChunk> chunks;
        for(const auto& block: hexData) {
            chunks.emplace_back(common::VBFChunk(block.first, block.second, crc16(block.second.data(), block.second.size())));
        }
        return {{}, chunks};
    }
    input.seekg (0, input.end);
    const int length = input.tellg();
    if (length < 0) {
        return {{}, {}};
    }
    input.seekg (0, input.beg);
    std::vector<uint8_t> data(length);
    input.read(reinterpret_cast<char*>(data.data()), data.size());
    if (input) {
        return createVbfFromBinary(carPlatform, ecuId, additionalData, std::move(data));
    }
    return {{}, {}};
}

}
