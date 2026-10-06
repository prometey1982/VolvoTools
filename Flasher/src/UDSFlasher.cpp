#include "flasher/UDSFlasher.hpp"

#include <j2534/J2534.hpp>
#include <j2534/J2534Channel.hpp>

#include <common/CommonData.hpp>
#include <common/CanIdProvider.hpp>
#include <common/protocols/UDSMessage.hpp>
#include "common/protocols/UDSRequest.hpp"
#include <common/protocols/UDSProtocolCommonSteps.hpp>

#define LOG_MODULE_NAME "flasher"
#include <common/LogHelper.hpp>
#include <common/ICanChannel.hpp>
#include <common/Util.hpp>

#define HFSM2_ENABLE_ALL
#include <common/hfsm2/machine.hpp>

#include <algorithm>

namespace flasher {

namespace {

/// Полуинтервал [begin, end) в адресном пространстве ЭБУ.
struct BlockRange
{
    uint64_t begin;
    uint64_t end;
};

/// Диапазон, который разрешено стирать: из VBF либо 1:1 с блоком прошивки.
BlockRange toRange(const common::DataBlock& block)
{
    return { block.startAddr, static_cast<uint64_t>(block.startAddr) + block.length };
}

BlockRange toRange(const common::VBFChunk& chunk)
{
    return { chunk.writeOffset, static_cast<uint64_t>(chunk.writeOffset) + chunk.data.size() };
}

std::vector<BlockRange> toBlockRanges(const std::vector<common::DataBlock>& blocks)
{
    std::vector<BlockRange> result;
    result.reserve(blocks.size());
    for(const auto& block: blocks) {
        result.push_back(toRange(block));
    }
    return result;
}

std::vector<BlockRange> toBlockRanges(const std::vector<common::VBFChunk>& chunks)
{
    std::vector<BlockRange> result;
    result.reserve(chunks.size());
    for(const auto& chunk: chunks) {
        result.push_back(toRange(chunk));
    }
    return result;
}

bool intersects(const BlockRange& lhs, const BlockRange& rhs)
{
    return lhs.begin < rhs.end && rhs.begin < lhs.end;
}

/// Участки диапазона, не покрытые ни одним блоком прошивки: после стирания их нечем восстановить.
std::vector<BlockRange> uncoveredRanges(const BlockRange& candidate,
                                        const std::vector<BlockRange>& chunkRanges)
{
    std::vector<BlockRange> covered;
    for(const auto& chunkRange: chunkRanges) {
        const BlockRange clipped{ std::max(chunkRange.begin, candidate.begin),
                                  std::min(chunkRange.end, candidate.end) };
        if(clipped.begin < clipped.end) {
            covered.push_back(clipped);
        }
    }
    std::sort(covered.begin(), covered.end(), [](const BlockRange& lhs, const BlockRange& rhs) {
        return lhs.begin < rhs.begin;
    });

    std::vector<BlockRange> result;
    uint64_t coveredTill = candidate.begin;
    for(const auto& range: covered) {
        if(coveredTill < range.begin) {
            result.push_back({ coveredTill, range.begin });
        }
        coveredTill = std::max(coveredTill, range.end);
    }
    if(coveredTill < candidate.end) {
        result.push_back({ coveredTill, candidate.end });
    }
    return result;
}

} // namespace

    class UDSFlasherImpl {
    public:
        UDSFlasherImpl(const std::vector<std::unique_ptr<common::ICanChannel>>& channels,
                       common::CarPlatform carPlatform,
                       uint32_t ecuId,
                       const UDSFlasherConfig& config,
                       std::unique_ptr<common::CanIdProvider> canIdProvider,
                       const std::function<void(FlasherState)>& stateUpdater,
                       const std::function<void(size_t)>& progressUpdater,
                       const std::function<void(size_t)>& maxProgressUpdater)
            : _channels{ channels }
            , _carPlatform{ carPlatform }
            , _ecuId{ ecuId }
            , _config{ config }
            , _canIdProvider{ std::move(canIdProvider) }
            , _isFailed{ false }
            , _stateUpdater{ stateUpdater }
            , _progressUpdater{ progressUpdater }
            , _eraseCandidates{ buildEraseCandidates(_config.flash) }
            , _blocksToErase{ _eraseCandidates }
            , _blocksToWrite(_config.flash.chunks.size(), true)
            , _maxProgressUpdater{ maxProgressUpdater }
        {
        }

        size_t getMaximumProgress()
        {
            return FlasherBase::getProgressFromVBF(_config.bootloader) + FlasherBase::getProgressFromVBF(_config.flash);
        }

        void fallAsleep()
        {
            _stateUpdater(FlasherState::FallAsleep);
            if (!common::UDSProtocolCommonSteps::fallAsleep(_channels, _canIdProvider->getFuncCanId())) {
                setFailed("Fall asleep failed");
            }
        }

        void startProgrammingSession()
        {
            _stateUpdater(FlasherState::ProgrammingSession);
            auto& channel{ common::getChannelByEcuId(_carPlatform, _ecuId, _channels) };
            if (!common::UDSProtocolCommonSteps::startProgrammingSession(channel, _canIdProvider->getPhysCanId())) {
                setFailed("Enter programming sesssion failed");
            }
        }

        void keepAlive()
        {
            auto& channel{ common::getChannelByEcuId(_carPlatform, _ecuId, _channels) };
            common::UDSProtocolCommonSteps::keepAlive(channel, _canIdProvider->getFuncCanId());
        }

        void authorize()
        {
            _stateUpdater(FlasherState::Authorize);
            auto& channel{ common::getChannelByEcuId(_carPlatform, _ecuId, _channels) };
            if (!common::UDSProtocolCommonSteps::authorize(channel, _canIdProvider->getPhysCanId(), _config.pin)) {
                setFailed("Authorization failed");
            }
        }

        void loadBootloader()
        {
            _stateUpdater(FlasherState::LoadBootloader);
            if (!_config.bootloader.chunks.empty()) {
                auto& channel{ common::getChannelByEcuId(_carPlatform, _ecuId, _channels) };
                if (!common::UDSProtocolCommonSteps::transferData(channel, _canIdProvider->getPhysCanId(), _config.bootloader,
                                                                                                _progressUpdater)) {
                    setFailed("Bootloader loading failed");
                }
            }
        }

        void startBootloader()
        {
            _stateUpdater(FlasherState::StartBootloader);
            if (!_config.bootloader.chunks.empty()) {
                auto& channel{ common::getChannelByEcuId(_carPlatform, _ecuId, _channels) };
                if (!common::UDSProtocolCommonSteps::startRoutine(channel, _canIdProvider->getPhysCanId(), _config.bootloader.header.call)) {
                    setFailed("Bootloader starting failed");
                }
            }
        }

        /// Считает контрольные суммы блоков прошивки и определяет, что нужно стереть и записать.
        /// Наборы выбираются итеративно: стирание диапазона уничтожает лежащие в нём блоки, их
        /// приходится дописывать, а это может потребовать стирания новых диапазонов. Итерации
        /// идут до стабилизации наборов, после чего план применяется целиком.
        void calculateBlocksToWrite()
        {
            _stateUpdater(FlasherState::CheckFlash);
            auto& channel{ common::getChannelByEcuId(_carPlatform, _ecuId, _channels) };
            const auto canId = _canIdProvider->getPhysCanId();
            const auto& chunks = _config.flash.chunks;

            if (!canFlashPartially()) {
                applyFullRewrite();
                return;
            }

            // Шаг 1. Опрос ЭБУ: какие блоки уже совпадают. Блок, контрольную сумму которого
            // получить не удалось, считаем требующим записи — не проверили, значит пишем.
            std::vector<bool> writeNeeded(chunks.size(), false);
            size_t differing = 0;
            size_t failed = 0;
            for(size_t i = 0; i < chunks.size(); ++i) {
                const auto& chunk = chunks[i];
                const common::DataBlock block{ chunk.writeOffset, static_cast<uint32_t>(chunk.data.size()) };
                uint16_t crc = 0;
                if (!common::UDSProtocolCommonSteps::getChunkCRC16(channel, canId, block, crc)) {
                    ++failed;
                    writeNeeded[i] = true;
                    LOG_MODULE(DEBUG) << "flash block " << std::hex << chunk.writeOffset
                                      << " - crc request failed, will write";
                    continue;
                }
                if (crc != (chunk.crc & 0xFFFF)) {
                    ++differing;
                    writeNeeded[i] = true;
                    LOG_MODULE(DEBUG) << "flash block " << std::hex << chunk.writeOffset
                                      << " - crc 0x" << static_cast<uint32_t>(crc)
                                      << " != expected 0x" << (chunk.crc & 0xFFFF) << ", will write";
                }
                else {
                    LOG_MODULE(DEBUG) << "flash block " << std::hex << chunk.writeOffset
                                      << " - crc 0x" << static_cast<uint32_t>(crc) << " matches, skip";
                }
            }
            LOG_MODULE(INFO) << "flash check finished: " << std::dec << chunks.size() << " blocks, "
                             << differing << " differ, " << failed << " crc requests failed";
            if (chunks.size() > 1 && differing + failed == chunks.size()) {
                LOG_MODULE(WARNING) << "all " << chunks.size()
                                    << " blocks differ from the image on the ECU,"
                                    << " full erase and write will be done";
            }

            const auto chunkRanges = toBlockRanges(chunks);
            const auto eraseRanges = toBlockRanges(_eraseCandidates);
            std::vector<bool> eraseNeeded(_eraseCandidates.size(), false);

            // Шаг 2. Итеративное расширение наборов до неподвижной точки.
            const size_t maxIterations = chunks.size() + _eraseCandidates.size() + 1;
            size_t iterations = 0;
            bool changed = true;
            while (changed) {
                if (++iterations > maxIterations) {
                    LOG_MODULE(ERROR) << "flash plan did not converge in " << maxIterations << " iterations";
                    applyFullRewrite();
                    return;
                }
                changed = false;
                // 2a. Записываемый блок требует стирания всех перекрытых им диапазонов.
                for(size_t i = 0; i < chunks.size(); ++i) {
                    if (!writeNeeded[i]) {
                        continue;
                    }
                    for(size_t j = 0; j < _eraseCandidates.size(); ++j) {
                        if (!eraseNeeded[j] && intersects(chunkRanges[i], eraseRanges[j])) {
                            eraseNeeded[j] = true;
                            changed = true;
                        }
                    }
                }
                // 2b. Всё, что попало в стираемый диапазон, будет уничтожено — надо записать.
                for(size_t j = 0; j < _eraseCandidates.size(); ++j) {
                    if (!eraseNeeded[j]) {
                        continue;
                    }
                    for(size_t i = 0; i < chunks.size(); ++i) {
                        if (!writeNeeded[i] && intersects(chunkRanges[i], eraseRanges[j])) {
                            writeNeeded[i] = true;
                            changed = true;
                        }
                    }
                }
            }

            // Перекрывающиеся блок и диапазон всегда либо оба в плане, либо оба вне его: иначе
            // часть блока останется нестёртой либо стёртое не будет восстановлено.
            for(size_t i = 0; i < chunks.size(); ++i) {
                for(size_t j = 0; j < _eraseCandidates.size(); ++j) {
                    if (writeNeeded[i] != eraseNeeded[j] && intersects(chunkRanges[i], eraseRanges[j])) {
                        LOG_MODULE(ERROR) << "flash plan invariant is broken for block " << std::hex
                                          << chunks[i].writeOffset << " and erase range 0x"
                                          << _eraseCandidates[j].startAddr;
                        applyFullRewrite();
                        return;
                    }
                }
            }

            applyPlan(writeNeeded, eraseNeeded);
        }

        void eraseFlash()
        {
            _stateUpdater(FlasherState::EraseFlash);
            auto& channel{ common::getChannelByEcuId(_carPlatform, _ecuId, _channels) };
            for(const auto& block: _blocksToErase) {
                LOG_MODULE(DEBUG) << "erase range " << std::hex << block.startAddr
                                  << ", size " << block.length;
                if (!common::UDSProtocolCommonSteps::eraseChunk(channel, _canIdProvider->getPhysCanId(), block)) {
                    setFailed("Flash erasing failed");
                    break;
                }
            }
        }

        void writeFlash()
        {
            _stateUpdater(FlasherState::WriteFlash);
            auto& channel{ common::getChannelByEcuId(_carPlatform, _ecuId, _channels) };
            for(size_t i = 0; i < _config.flash.chunks.size(); ++i) {
                const auto& chunk = _config.flash.chunks[i];
                if (!_blocksToWrite[i]) {
                    LOG_MODULE(DEBUG) << "flash block " << std::hex << chunk.writeOffset
                                      << " is up to date, skip writing";
                    continue;
                }
                if (!common::UDSProtocolCommonSteps::transferChunk(channel, _canIdProvider->getPhysCanId(), chunk,
                                                                    _progressUpdater)) {
                    setFailed("Flash writing failed");
                    break;
                }
            }
        }

        void checkValidApplication()
        {
            auto& channel{ common::getChannelByEcuId(_carPlatform, _ecuId, _channels) };
            common::UDSProtocolCommonSteps::checkValidApplication(channel, _canIdProvider->getPhysCanId());
        }

        void wakeUp()
        {
            _stateUpdater(FlasherState::WakeUp);
            common::UDSProtocolCommonSteps::wakeUp(_channels, _canIdProvider->getFuncCanId());
        }

        void closeChannels()
        {
            _stateUpdater(FlasherState::CloseChannels);
        }

        void done()
        {
            _stateUpdater(FlasherState::Done);
        }

        void error()
        {
            _stateUpdater(FlasherState::Error);
        }

        bool isFailed() const
        {
            return _isFailed;
        }

    private:
        void setFailed(const std::string& message)
        {
            LOG_MODULE(ERROR) << message;
            _isFailed = true;
            _errorMessage = message;
        }

        /// Диапазоны, которые разрешено стирать. Если VBF их не объявил, по договорённости
        /// гранулярность стирания совпадает с блоками прошивки — 1:1 по адресу и длине.
        static std::vector<common::DataBlock> buildEraseCandidates(const common::VBF& flash)
        {
            if (!flash.header.eraseBlocks.empty()) {
                return flash.header.eraseBlocks;
            }
            std::vector<common::DataBlock> result;
            result.reserve(flash.chunks.size());
            for(const auto& chunk: flash.chunks) {
                result.emplace_back(chunk.writeOffset, static_cast<uint32_t>(chunk.data.size()));
            }
            return result;
        }

        /// Частичная прошивка требует известных границ стираемых диапазонов: иначе непонятно,
        /// что именно уничтожит стирание. Диапазон нулевой длины (синтаксис "erase = 0x...;" в VBF)
        /// такой границей не является.
        bool canFlashPartially() const
        {
            if (_eraseCandidates.empty()) {
                LOG_MODULE(WARNING) << "no erase ranges declared, writing everything";
                return false;
            }
            for(const auto& candidate: _eraseCandidates) {
                if (candidate.length == 0) {
                    LOG_MODULE(WARNING) << "erase range 0x" << std::hex << candidate.startAddr
                                        << " has zero length, writing everything";
                    return false;
                }
            }
            return true;
        }

        /// Консервативный вариант: стереть все объявленные диапазоны и записать все блоки.
        /// Исходное состояние и откат, если план построить не удалось.
        void applyFullRewrite()
        {
            _blocksToWrite.assign(_config.flash.chunks.size(), true);
            _blocksToErase = _eraseCandidates;
            _maxProgressUpdater(FlasherBase::getProgressFromVBF(_config.bootloader)
                                + FlasherBase::getProgressFromVBF(_config.flash));
        }

        /// Применяет посчитанный план: что стирать, что писать и какой теперь максимум прогресса.
        void applyPlan(const std::vector<bool>& writeNeeded, const std::vector<bool>& eraseNeeded)
        {
            const auto& chunks = _config.flash.chunks;
            _blocksToWrite = writeNeeded;
            _blocksToErase.clear();
            size_t bytesToErase = 0;
            for(size_t j = 0; j < _eraseCandidates.size(); ++j) {
                if (eraseNeeded[j]) {
                    _blocksToErase.push_back(_eraseCandidates[j]);
                    bytesToErase += _eraseCandidates[j].length;
                }
            }
            std::sort(_blocksToErase.begin(), _blocksToErase.end(),
                      [](const common::DataBlock& lhs, const common::DataBlock& rhs) {
                          return lhs.startAddr < rhs.startAddr;
                      });

            size_t blocksToWrite = 0;
            size_t bytesToWrite = 0;
            for(size_t i = 0; i < chunks.size(); ++i) {
                if (writeNeeded[i]) {
                    ++blocksToWrite;
                    bytesToWrite += chunks[i].data.size();
                }
            }
            LOG_MODULE(INFO) << "flash plan: erase " << std::dec << _blocksToErase.size() << " of "
                             << _eraseCandidates.size() << " ranges (" << bytesToErase << " bytes), write "
                             << blocksToWrite << " of " << chunks.size() << " blocks ("
                             << bytesToWrite << " bytes)";
            logUncoveredAreas(eraseNeeded);
            // Максимум прогресса зависит от плана: писать будем только выбранные блоки.
            _maxProgressUpdater(FlasherBase::getProgressFromVBF(_config.bootloader) + bytesToWrite);
        }

        /// Участки стираемых диапазонов, не покрытые блоками прошивки: после стирания их нечем
        /// восстановить. Это ожидаемая ситуация (например, последний блок Denso restyling короче
        /// своего диапазона), поэтому только сообщаем.
        void logUncoveredAreas(const std::vector<bool>& eraseNeeded) const
        {
            const auto chunkRanges = toBlockRanges(_config.flash.chunks);
            for(size_t j = 0; j < _eraseCandidates.size(); ++j) {
                if (!eraseNeeded[j]) {
                    continue;
                }
                for(const auto& uncovered: uncoveredRanges(toRange(_eraseCandidates[j]), chunkRanges)) {
                    LOG_MODULE(INFO) << "erase range 0x" << std::hex << _eraseCandidates[j].startAddr
                                     << " is not covered by flash blocks: [0x" << uncovered.begin
                                     << ", 0x" << uncovered.end << ") will stay erased";
                }
            }
        }

    private:
        const std::vector<std::unique_ptr<common::ICanChannel>>& _channels;
        common::CarPlatform _carPlatform;
        uint32_t _ecuId;
        const UDSFlasherConfig& _config;
        std::unique_ptr<common::CanIdProvider> _canIdProvider;
        bool _isFailed;
        std::string _errorMessage;
        const std::function<void(FlasherState)> _stateUpdater;
        const std::function<void(size_t)> _progressUpdater;
        /// Диапазоны, которые разрешено стирать (из VBF или 1:1 с блоками прошивки).
        const std::vector<common::DataBlock> _eraseCandidates;
        /// Выбранные к стиранию диапазоны по возрастанию адреса, подмножество _eraseCandidates.
        std::vector<common::DataBlock> _blocksToErase;
        /// По каждому блоку прошивки: нужно ли его писать. По умолчанию — все, консервативно.
        std::vector<bool> _blocksToWrite;
        /// Пересчёт максимума прогресса: после проверки писать нужно меньше блоков.
        const std::function<void(size_t)> _maxProgressUpdater;
    };

using M = hfsm2::MachineT<hfsm2::Config::ContextT<UDSFlasherImpl&>>;
    using FSM = M::PeerRoot<
        M::Composite<
            struct StartWork,
            struct KeepAlive,
            struct FallAsleep,
            struct StartProgrammingSession,
            struct Authorize,
            struct LoadBootloader,
            struct StartBootloader,
            struct CheckFlash,
            struct EraseFlash,
            struct WriteFlash,
            struct CheckValidApplication>,
        M::Composite<
            struct Finish,
            struct WakeUp,
            struct Done,
            struct Error>
        >;

    struct BaseState : public FSM::State {
    public:
        void update(FullControl& control)
        {
            if (!control.context().isFailed()) {
                control.succeed();
            }
            else {
                control.fail();
            }
        }
    };

    struct BaseSuccesState : public FSM::State {
    public:
        void update(FullControl& control)
        {
            control.succeed();
        }
    };

    struct StartWork : public FSM::State {
        void enter(PlanControl& control)
        {
            auto plan = control.plan();
            plan.change<KeepAlive, FallAsleep>();
            plan.change<FallAsleep, StartProgrammingSession>();
            plan.change<StartProgrammingSession, Authorize>();
            plan.change<Authorize, LoadBootloader>();
            plan.change<LoadBootloader, StartBootloader>();
            plan.change<StartBootloader, CheckFlash>();
            plan.change<CheckFlash, EraseFlash>();
            plan.change<EraseFlash, WriteFlash>();
            plan.change<WriteFlash, CheckValidApplication>();
        }

        void planSucceeded(FullControl& control) {
            control.changeTo<Finish>();
        }

        void planFailed(FullControl& control)
        {
            control.changeTo<Finish>();
        }
    };

    struct FallAsleep : public BaseState {
        void enter(PlanControl& control)
        {
            control.context().fallAsleep();
        }
    };

    struct KeepAlive : public BaseState {
        void enter(PlanControl& control)
        {
            control.context().keepAlive();
        }
    };

    struct StartProgrammingSession : public BaseState {
        void enter(PlanControl& control)
        {
            control.context().startProgrammingSession();
        }
    };

    struct Authorize : public BaseState {
        void enter(PlanControl& control)
        {
            control.context().authorize();
        }
    };

    struct LoadBootloader : public BaseState {
        void enter(PlanControl& control)
        {
            control.context().loadBootloader();
        }
    };

    struct StartBootloader : public BaseState {
        void enter(PlanControl& control)
        {
            control.context().startBootloader();
        }
    };

    struct CheckFlash : public BaseState {
        void enter(PlanControl& control)
        {
            control.context().calculateBlocksToWrite();
        }
    };

    struct EraseFlash : public BaseState {
        void enter(PlanControl& control)
        {
            control.context().eraseFlash();
        }
    };

    struct WriteFlash : public BaseState {
        void enter(PlanControl& control)
        {
            control.context().writeFlash();
        }
    };

    struct CheckValidApplication : public BaseState {
        void enter(PlanControl& control)
        {
            control.context().checkValidApplication();
        }
    };

    struct Finish : public FSM::State {
        void enter(PlanControl& control)
        {
            auto plan = control.plan();
            if (control.context().isFailed()) {
                plan.change<WakeUp, Error>();
            }
            else {
                plan.change<WakeUp, Done>();
            }
        }
    };

    struct WakeUp : public BaseSuccesState {
        void enter(PlanControl& control)
        {
            control.context().wakeUp();
        }
    };

    struct Done : public BaseSuccesState {
        void enter(PlanControl& control)
        {
            control.context().done();
        }
    };

    struct Error : public BaseSuccesState {
        void enter(PlanControl& control)
        {
            control.context().error();
        }
    };

    UDSFlasher::UDSFlasher(j2534::J2534& j2534, common::CarPlatform carPlatform, uint32_t ecuId,
                           UDSFlasherConfig&& config)
        : FlasherBase{ j2534, carPlatform, ecuId }
        , _config{ std::move(config) }
    {
    }

    UDSFlasher::~UDSFlasher()
    {
    }

    void UDSFlasher::startImpl(const std::vector<std::unique_ptr<common::ICanChannel>>& channels)
    {
        const auto ecuInfo{ common::getEcuInfoByEcuId(_carPlatform, _ecuId) };

        UDSFlasherImpl impl(channels, _carPlatform, _ecuId, _config,
            common::createCanIdProviderForEcu(_carPlatform, _ecuId), [this](FlasherState state) {
            setCurrentState(state);
        },
            [this](size_t progress) {
                incCurrentProgress(progress);
            },
            [this](size_t maxProgress) {
                setMaximumProgress(maxProgress);
            });

        setMaximumProgress(impl.getMaximumProgress());

        FSM::Instance fsm{ impl };

        while(getCurrentState() != FlasherState::Done && getCurrentState() != FlasherState::Error) {
            fsm.update();
        }
    }

} // namespace flasher
