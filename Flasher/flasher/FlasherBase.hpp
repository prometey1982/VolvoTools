#pragma once

#include "FlasherState.hpp"
#include "SBLProviderBase.hpp"

#include <common/compression/CompressorBase.hpp>
#include <common/encryption/EncryptorBase.hpp>
#include <common/J2534ChannelProvider.hpp>


#include <functional>
#include <mutex>
#include <vector>

namespace flasher {

class FlasherCallback;

struct FlasherParameters {
    common::CarPlatform carPlatform;
    uint32_t ecuId;
    std::string additionalData;
    std::shared_ptr<SBLProviderBase> sblProvider;
    const common::VBF flash;
    std::unique_ptr<common::CompressorBase> compressor;
    std::unique_ptr<common::EncryptorBase> encryptor;
};

class FlasherBase {
public:
    static size_t getProgressFromVBF(const common::VBF& vbf);

    FlasherBase(j2534::J2534 &j2534, FlasherParameters&& flasherParameters);
    virtual ~FlasherBase();

    FlasherState getCurrentState() const;
    size_t getCurrentProgress() const;
    size_t getMaximumProgress() const;

    void registerCallback(FlasherCallback &callback);
    void unregisterCallback(FlasherCallback &callback);

    void start();

protected:
    virtual void startImpl(std::vector<std::unique_ptr<j2534::J2534Channel>>& channels) = 0;

    const FlasherParameters& getFlasherParameters() const;

    void setCurrentState(FlasherState state);
    void setCurrentProgress(size_t currentProgress);
    void incCurrentProgress(size_t delta);
    void setMaximumProgress(size_t maximumProgress);

    void runOnThread(std::function<void()> callable);

private:
    std::vector<FlasherCallback *> getCallbacks() const;

private:
    common::J2534ChannelProvider _j2534ChannelProvider;
    FlasherParameters _flasherParameters;
    mutable std::mutex _mutex;
    size_t _currentProgress;
    size_t _maximumProgress;
    FlasherState _currentState;

    std::thread _flasherThread;

    std::vector<FlasherCallback *> _callbacks;

    bool _stopRequested;
};

} // namespace flasher
