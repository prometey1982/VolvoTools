#pragma once

#include "flasher/FlasherState.hpp"

#include <common/CarPlatform.hpp>
#include <common/VBF.hpp>

#include <cstdint>
#include <functional>
#include <memory>
#include <vector>

namespace common {
class ICanChannel;
} // namespace common

namespace flasher {

class D2FlasherImpl {
public:
    D2FlasherImpl(const std::vector<std::unique_ptr<common::ICanChannel>>& channels,
                   common::CarPlatform carPlatform,
                   uint8_t ecuId,
                   const common::VBF& bootloader,
                   const std::function<void(FlasherState)>& stateUpdater,
                   const std::function<void(size_t)>& progressUpdater,
                   const std::function<void(common::ICanChannel&, uint8_t)>& eraseCallback,
                   const std::function<void(common::ICanChannel&, uint8_t)>& writeCallback);

    void run();

    void setMaximumFlashProgressValue(size_t value);
    size_t getMaximumProgress() const;

    bool isFailed() const;
    bool isSBLRequired() const;

    void wakeUpChannels();
    void fallAsleep();
    void startPBL();
    void loadSBL();
    void startSBL();
    void eraseFlash();
    void writeFlash();
    void wakeUpFinish();
    void setDIMTime();
    void done();
    void error();

private:
    size_t getMaximumFlashProgressValue() const;
    void setFailed(const std::string& msg);

    const std::vector<std::unique_ptr<common::ICanChannel>>& _channels;
    common::CarPlatform _carPlatform;
    uint8_t _ecuId;
    common::VBF _bootloader;
    bool _isFailed = false;
    bool _isDone = false;
    std::string _errorMessage;
    size_t _maximumFlashProgress = 0;
    const std::function<void(FlasherState)> _stateUpdater;
    const std::function<void(size_t)> _progressUpdater;
    const std::function<void(common::ICanChannel&, uint8_t)> _eraseCallback;
    const std::function<void(common::ICanChannel&, uint8_t)> _writeCallback;
};

} // namespace flasher
