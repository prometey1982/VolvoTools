#pragma once

#include "UDSProtocolStep.hpp"

#include <cinttypes>
#include <mutex>
#include <vector>

namespace j2534 {
    class J2534;
    class J2534Channel;
} // namespace j2534

namespace common {

    class UDSProtocolCallback;
    class UDSProtocolStep;

    class UDSProtocolBase {
    public:
        UDSProtocolBase(j2534::J2534& j2534, uint32_t canId);
        virtual ~UDSProtocolBase();

        void run();

        enum class State { Initial, InProgress, Done, Error };

        void registerCallback(UDSProtocolCallback& callback);
        void unregisterCallback(UDSProtocolCallback& callback);

        State getState() const;
        size_t getCurrentProgress() const;
        size_t getMaximumProgress() const;

    protected:
        j2534::J2534& getJ2534() const;
        uint32_t getCanId() const;

        void registerStep(std::unique_ptr<UDSProtocolStep>&& step);
        void setState(State newState);

        std::mutex& getMutex() const;

    private:
        void stepToCallbacks(common::UDSStepType stepType);

        j2534::J2534& _j2534;
        uint32_t _canId;
        mutable std::mutex _mutex;
        State _currentState;
        std::vector<std::unique_ptr<j2534::J2534Channel>> _channels;
        std::vector<std::unique_ptr<UDSProtocolStep>> _steps;
        std::vector<UDSProtocolCallback*> _callbacks;
    };

} // namespace common
