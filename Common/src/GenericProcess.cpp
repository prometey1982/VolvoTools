#include "common/GenericProcess.hpp"

namespace common {

GenericProcess::GenericProcess()
    : _currentState{ GenericProcessState::Initial }
{
}

GenericProcessState GenericProcess::getState() const
{
    std::unique_lock<std::mutex> lock(_mutex);
    return _currentState;
}

void GenericProcess::setState(GenericProcessState newState)
{
    std::unique_lock<std::mutex> lock(_mutex);
    _currentState = newState;
}

} // namespace common
