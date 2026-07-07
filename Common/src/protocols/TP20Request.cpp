#include "common/protocols/TP20Request.hpp"

#include "common/protocols/TP20Error.hpp"
#include "common/CanFrame.hpp"
#include "common/ICanChannel.hpp"
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
        : _canId{ canId }
        , _requestId{ getRequestId(data) }
        , _responseCanId{ responseCanId }
        , _data{ data }
    {
    }

    TP20Request::TP20Request(uint32_t canId, uint32_t responseCanId, std::vector<uint8_t>&& data)
        : _canId{ canId }
        , _requestId{ getRequestId(data) }
        , _responseCanId{ responseCanId }
        , _data{ std::move(data) }
    {
    }

    std::vector<uint8_t> TP20Request::process(ICanChannel& channel, size_t timeout) const
    {
        if (!channel.send({_canId, _data})) {
            throw std::runtime_error("Failed to send CAN message");
        }
        std::vector<uint8_t> result;
        while (true) {
            CanFrame response;
            if (!channel.receive(response, static_cast<unsigned long>(timeout))) {
                throw std::runtime_error("Failed to receive response");
            }
            checkTP20Error(_requestId, response.data.data(), response.data.size());
            if (response.id != _responseCanId) {
                continue;
            }
            if (response.data.empty()) {
                continue;
            }
            result = response.data;
            break;
        }
        return result;
    }

} // namespace common
