#include "common/protocols/TP20Request.hpp"

#include "common/protocols/TP20Error.hpp"
#include "common/Util.hpp"

#include <algorithm>
#include <stdexcept>
#include <iterator>

namespace common {

    namespace {

        uint8_t getRequestId(const std::vector<uint8_t>& data)
        {
            if (data.empty()) {
                throw std::runtime_error("Can't get request id from empty request");
            }
            return data[0];
        }

    }

    TP20Request::TP20Request(uint32_t canId, uint32_t responseCanId, const std::vector<uint8_t>& data)
        : _requestId{ getRequestId(data) }
        , _responseCanId{ responseCanId }
        , _message{ canId, data }
    {
    }

    TP20Request::TP20Request(uint32_t canId, uint32_t responseCanId, std::vector<uint8_t>&& data)
        : _requestId{ getRequestId(data) }
        , _responseCanId{ responseCanId }
        , _message{ canId, std::move(data) }
    {
    }

    std::vector<uint8_t> TP20Request::process(const j2534::J2534Channel& channel, size_t timeout) const
    {
        unsigned long numMsgs = 0;
        if (channel.writeMsgs(_message, numMsgs, timeout) != STATUS_NOERROR || numMsgs < 1) {
            throw std::runtime_error("Failed to send CAN message");
        }
        std::vector<uint8_t> result;
        channel.readMsgs([&result, this](const uint8_t* data, size_t dataSize) {
            checkTP20Error(_requestId, data, dataSize);
            if (dataSize < 4) {
                return true;
            }
            const auto receivedCanId{ encodeBigEndian(data[3], data[2], data[1], data[0]) };
            if (receivedCanId != _responseCanId) {
                return true;
            }
            size_t dataOffset = 4;
            result.reserve(result.size() + dataSize - dataOffset);
            std::copy(data + dataOffset, data + dataSize, std::back_inserter(result));
            return false;
            }, timeout);
        return result;
    }

} // namespace common
