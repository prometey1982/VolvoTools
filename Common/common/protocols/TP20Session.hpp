#pragma once

#include "common/CarPlatform.hpp"

#include <j2534/J2534Channel.hpp>

#include <chrono>
#include <memory>

namespace common {

    class TP20Request;

    class TP20Session {
    public:
        TP20Session(const j2534::J2534Channel& channel, CarPlatform carPlatform, uint8_t ecuId);
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
