#pragma once

#include "common/CarPlatform.hpp"

#include <chrono>
#include <memory>

class ICanChannel;

namespace common {

    class TP20Request;

    class TP20Session {
    public:
        TP20Session(ICanChannel& channel, CarPlatform carPlatform, uint8_t ecuId);
        ~TP20Session();

        bool start();
        void stop();

        std::vector<uint8_t> process(const std::vector<uint8_t>& request) const;

        bool writeMessage(const std::vector<uint8_t>& request) const;
        std::vector<uint8_t> readMessage(size_t timeout = 1000) const;

    private:
        std::unique_ptr<class TP20SessionImpl> _impl;
    };

} // namespace common
