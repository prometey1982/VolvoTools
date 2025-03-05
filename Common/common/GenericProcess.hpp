#pragma once

#include "GenericProcessState.hpp"

#include <mutex>

namespace common {

class GenericProcess {
public:
    GenericProcess();

    GenericProcessState getState() const;

protected:
    void setState(GenericProcessState newState);

private:
    mutable std::mutex _mutex;
    GenericProcessState _currentState;
};

} // namespace common
