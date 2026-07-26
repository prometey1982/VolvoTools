#include "flasher/pin/DummyPinCrackerSteps.hpp"

#include "common/ICanChannel.hpp"
#include "common/Util.hpp"

#include <thread>

namespace flasher {

bool DummyPinCrackerSteps::preAuth([[maybe_unused]]common::ICanChannel& channel)
{
    return true;
}

void DummyPinCrackerSteps::postAuth([[maybe_unused]]common::ICanChannel& channel)
{
}

bool DummyPinCrackerSteps::tryPin([[maybe_unused]]common::ICanChannel& channel, [[maybe_unused]]uint64_t pin)
{
    return false;
}

std::vector<unsigned long> DummyPinCrackerSteps::startKeepAlive([[maybe_unused]]common::ICanChannel& channel)
{
    return {};
}

void DummyPinCrackerSteps::stopKeepAlive([[maybe_unused]]const std::vector<unsigned long>& ids)
{
}

} // namespace flasher
