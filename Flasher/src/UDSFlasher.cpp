#include "flasher/UDSFlasher.hpp"

#include <j2534/J2534.hpp>
#include <j2534/J2534Channel.hpp>

#include <common/UDSMessage.hpp>
#include <common/UDSProtocolCommonSteps.hpp>
#include <common/Util.hpp>

#include <optional>
#include <unordered_map>

namespace flasher {

    UDSFlasher::UDSFlasher(j2534::J2534& j2534, uint32_t cmId, const std::array<uint8_t, 5>& pin,
        const common::VBF& bootloader, const common::VBF& flash)
        : UDSProtocolBase{ j2534, cmId }
        , _pin{ pin }
        , _bootloader{ bootloader }
        , _flash{ flash }
    {
        registerStep(std::make_unique<common::OpenChannelsStep>(getJ2534(), getCanId(), _channels));
        registerStep(std::make_unique<common::FallingAsleepStep>(_channels));
        registerStep(std::make_unique<common::KeepAliveStep>(_channels, getCanId()));
        registerStep(std::make_unique<common::AuthorizingStep>(_channels, getCanId(), _pin));
        registerStep(std::make_unique<common::DataTransferStep>(common::UDSStepType::BootloaderLoading, _channels, getCanId(), _bootloader));
        registerStep(std::make_unique<common::StartRoutineStep>(_channels, getCanId(), _bootloader.header));
        registerStep(std::make_unique<common::FlashErasingStep>(_channels, getCanId(), _flash));
        registerStep(std::make_unique<common::DataTransferStep>(common::UDSStepType::FlashLoading, _channels, getCanId(), _flash));
        registerStep(std::make_unique<common::WakeUpStep>(_channels));
        registerStep(std::make_unique<common::CloseChannelsStep>(_channels));
    }

    UDSFlasher::~UDSFlasher()
    {
    }

    void UDSFlasher::start()
    {
        _thread = std::thread([this] {
            run();
            });
    }

#if 0
    void UDSFlasher::registerCallback(FlasherCallback& callback)
    {
        std::unique_lock<std::mutex> lock{ getMutex() };
        _callbacks.push_back(&callback);
    }

    void UDSFlasher::unregisterCallback(FlasherCallback& callback)
    {
        std::unique_lock<std::mutex> lock{ getMutex() };
        _callbacks.erase(std::remove(_callbacks.begin(), _callbacks.end(), &callback),
            _callbacks.end());
    }

    void UDSFlasher::messageToCallbacks(const std::string& message) {
        decltype(_callbacks) tmpCallbacks;
        {
            std::unique_lock<std::mutex> lock(getMutex());
            tmpCallbacks = _callbacks;
        }
        for (const auto& callback : tmpCallbacks) {
            callback->OnMessage(message);
        }
    }
#endif

} // namespace flasher
