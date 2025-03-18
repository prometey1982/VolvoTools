#pragma once

#include "common/CarPlatform.hpp"

#include <j2534/J2534Channel.hpp>

#include <chrono>
#include <memory>

namespace common {

    class TP20Request;

    class TP20Session {
    public:
        TP20Session(j2534::J2534Channel& channel, CarPlatform carPlatform, uint8_t ecuId);
        ~TP20Session();

        bool start();
        void stop();

        std::vector<uint8_t> process(std::vector<uint8_t>&& request);

    private:
        std::unique_ptr<class TP20SessionImpl> _impl;
    };

} // namespace common
