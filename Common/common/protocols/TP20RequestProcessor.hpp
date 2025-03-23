#pragma once

#include "RequestProcessorBase.hpp"
#include "TP20Session.hpp"

namespace common {

    class TP20RequestProcessor: public RequestProcessorBase {
    public:
        explicit TP20RequestProcessor(TP20Session& session);

        virtual std::vector<uint8_t> process(std::vector<uint8_t>&& service, std::vector<uint8_t>&& params, size_t timeout) const override;
        virtual void disconnect() override;
        virtual bool connect() override;

    private:
        TP20Session& _session;
    };

} // namespace common
