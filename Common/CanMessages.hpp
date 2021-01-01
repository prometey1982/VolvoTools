#pragma once

#include "CEMCanMessage.hpp"

namespace common {

static const CEMCanMessage requestVIN{
    common::CEMCanMessage::makeCanMessage(common::ECUType::CEM, {0xB9, 0xFB}) };
static const CEMCanMessage requestMemory{
    common::CEMCanMessage::makeCanMessage(common::ECUType::ECM_ME, {0xA6, 0xF0, 0x00, 0x01}) };
static const CEMCanMessage unregisterAllMemoryRequest{
    common::CEMCanMessage::makeCanMessage(common::ECUType::ECM_ME, {0xAA, 0x00}) };
static const CEMCanMessage wakeUpCanRequest{
    common::CEMCanMessage::makeCanMessage(0xFF, 0xC8) };
static const CEMCanMessage goToSleepCanRequest{
    common::CEMCanMessage::makeCanMessage(0xFF, 0x86) };

} // namespace common
