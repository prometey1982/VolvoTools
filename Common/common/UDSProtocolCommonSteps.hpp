#pragma once

#include "UDSProtocolStep.hpp"
#include "VBF.hpp"

#include <j2534/J2534.hpp>
#include <j2534/J2534Channel.hpp>


namespace common {

    class OpenChannelsStep : public UDSProtocolStep {
    public:
        OpenChannelsStep(j2534::J2534& j2534, uint32_t cmId, std::vector<std::unique_ptr<j2534::J2534Channel>>& channels);

        bool processImpl() override;

    private:
        j2534::J2534& _j2534;
        uint32_t _cmId;
        std::vector<std::unique_ptr<j2534::J2534Channel>>& _channels;
    };

    class CloseChannelsStep : public UDSProtocolStep {
    public:
        CloseChannelsStep(std::vector<std::unique_ptr<j2534::J2534Channel>>& channels);

        bool processImpl() override;

    private:
        std::vector<std::unique_ptr<j2534::J2534Channel>>& _channels;
    };

    class FallingAsleepStep : public UDSProtocolStep {
    public:
        FallingAsleepStep(const std::vector<std::unique_ptr<j2534::J2534Channel>>& channels);

        bool processImpl() override;

    private:
        const std::vector<std::unique_ptr<j2534::J2534Channel>>& _channels;
    };

    class KeepAliveStep : public UDSProtocolStep {
    public:
        KeepAliveStep(const std::vector<std::unique_ptr<j2534::J2534Channel>>& channels, uint32_t cmId);

        bool processImpl() override;

    private:
        const std::vector<std::unique_ptr<j2534::J2534Channel>>& _channels;
        uint32_t _cmId;
    };

    class WakeUpStep : public UDSProtocolStep {
    public:
        WakeUpStep(const std::vector<std::unique_ptr<j2534::J2534Channel>>& channels);

        bool processImpl() override;

    private:
        const std::vector<std::unique_ptr<j2534::J2534Channel>>& _channels;
    };

    class AuthorizingStep : public UDSProtocolStep {
    public:
        AuthorizingStep(const std::vector<std::unique_ptr<j2534::J2534Channel>>& channels,
            uint32_t cmId, const std::array<uint8_t, 5>& pin);

        bool processImpl() override;

    private:
        uint32_t generateKeyImpl(uint32_t hash, uint32_t input);
        uint32_t generateKey(const std::array<uint8_t, 5>& pin_array, const std::array<uint8_t, 3>& seed_array);

        const std::vector<std::unique_ptr<j2534::J2534Channel>>& _channels;
        uint32_t _cmId;
        const std::array<uint8_t, 5>& _pin;
    };

    class DataTransferStep : public UDSProtocolStep {
    public:
        DataTransferStep(UDSStepType step, const std::vector<std::unique_ptr<j2534::J2534Channel>>& channels, uint32_t cmId,
            const VBF& data);

        bool processImpl() override;

    private:
        size_t getMaximumSize(const VBF& data);

        const std::vector<std::unique_ptr<j2534::J2534Channel>>& _channels;
        uint32_t _cmId;
        const VBF& _data;
    };

    class FlashErasingStep : public UDSProtocolStep {
    public:
        FlashErasingStep(const std::vector<std::unique_ptr<j2534::J2534Channel>>& channels, uint32_t cmId, const VBF& flash);

        bool processImpl() override;

    private:
        const std::vector<std::unique_ptr<j2534::J2534Channel>>& _channels;
        uint32_t _cmId;
        const VBF& _flash;
    };

    class StartRoutineStep : public UDSProtocolStep {
    public:
        StartRoutineStep(const std::vector<std::unique_ptr<j2534::J2534Channel>>& channels, uint32_t cmId, const VBFHeader& header);

        bool processImpl() override;

    private:
        const std::vector<std::unique_ptr<j2534::J2534Channel>>& _channels;
        uint32_t _cmId;
        const VBFHeader& _header;
    };

} // namespace common
