#include "common/protocols/TP20RequestProcessor.hpp"

#include "common/protocols/UDSRequest.hpp"

namespace common {

    TP20RequestProcessor::TP20RequestProcessor(const TP20Session& session)
        : _session{ session }
    {
    }

    std::vector<uint8_t> TP20RequestProcessor::process(std::vector<uint8_t>&& data, size_t) const
    {
        return _session.process(data);
    }

} // namespace common
