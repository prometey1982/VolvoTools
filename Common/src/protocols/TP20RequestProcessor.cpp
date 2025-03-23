#include "common/protocols/TP20RequestProcessor.hpp"

#include "common/protocols/TP20Error.hpp"
#include "common/Util.hpp"

namespace common {

    TP20RequestProcessor::TP20RequestProcessor(const TP20Session& session)
        : _session{ session }
    {
    }

    std::vector<uint8_t> TP20RequestProcessor::process(std::vector<uint8_t>&& service, std::vector<uint8_t>&& params, size_t) const
    {
        auto fullRequest{ service };
        fullRequest.insert(fullRequest.end(), params.cbegin(), params.cend());
        const auto requestId{ service[0] };
        const auto responseId{ requestId + 0x40 };
        service[0] = responseId;
        if (!_session.writeMessage(fullRequest)) {
            throw std::runtime_error("Can't write message");
        }
        while (true) {
            const auto response{ _session.readMessage() };
            if (response.empty()) {
                throw std::runtime_error("Empty response");
            }
            try {
                checkTP20Error(requestId, response.data(), response.size());
            }
            catch (const TP20Error& er) {
                if (er.getErrorCode() == TP20Error::ErrorCode::BusyResponsePending) {
                    continue;
                }
                throw;
            }
            if (std::equal(service.cbegin(), service.cend(), response.cbegin())) {
                return response;
            }
        }
    }

} // namespace common
