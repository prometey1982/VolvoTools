#define BOOST_TEST_MODULE D2Flasher
#include <boost/test/unit_test.hpp>

#include <easylogging++.h>
INITIALIZE_EASYLOGGINGPP

#include "../src/D2FlasherImpl.hpp"
#include <common/ICanChannel.hpp>
#include <common/VBF.hpp>
#include <common/CarPlatform.hpp>

#include "MockICanChannel.hpp"

#include <memory>
#include <vector>

using namespace flasher;

// ---------------------------------------------------------------------------
// Fixture
// ---------------------------------------------------------------------------
struct D2FlasherFixture {
    MockICanChannel mock1;
    MockICanChannel mock2;
    std::vector<std::unique_ptr<ICanChannel>> channels;
    common::VBF emptyVbf{common::VBFHeader{}, {}};
    common::VBF bootloaderVbf{common::VBFHeader{}, {}};

    D2FlasherFixture() {
        channels.emplace_back(std::make_unique<MockChannelWrapper>(mock1));
        channels.emplace_back(std::make_unique<MockChannelWrapper>(mock2));
    }
};

// ---------------------------------------------------------------------------
// Test: SBL detection
// ---------------------------------------------------------------------------
BOOST_FIXTURE_TEST_CASE(SBLDetection, D2FlasherFixture)
{
    D2FlasherImpl impl(channels, common::CarPlatform::P2, 0x7A, emptyVbf,
        [](FlasherState) {}, [](size_t) {},
        [](ICanChannel&, uint8_t) {}, [](ICanChannel&, uint8_t) {});

    BOOST_CHECK(!impl.isSBLRequired());

    common::VBFChunk chunk{0x8000, {0x01, 0x02, 0x03, 0x04}};
    bootloaderVbf.chunks.push_back(std::move(chunk));

    D2FlasherImpl impl2(channels, common::CarPlatform::P2, 0x7A, bootloaderVbf,
        [](FlasherState) {}, [](size_t) {},
        [](ICanChannel&, uint8_t) {}, [](ICanChannel&, uint8_t) {});

    BOOST_CHECK(impl2.isSBLRequired());
}

// ---------------------------------------------------------------------------
// Test: wakeUpChannels calls send on all channels
// ---------------------------------------------------------------------------
BOOST_FIXTURE_TEST_CASE(WakeUpSendsOnAllChannels, D2FlasherFixture)
{
    int stateUpdates = 0;
    D2FlasherImpl impl(channels, common::CarPlatform::P2, 0x7A, emptyVbf,
        [&](FlasherState s) {
            if (s == FlasherState::WakeUp) ++stateUpdates;
        },
        [](size_t) {},
        [](ICanChannel&, uint8_t) {}, [](ICanChannel&, uint8_t) {});

    impl.wakeUpChannels();

    BOOST_CHECK(stateUpdates == 1);
    BOOST_CHECK(mock1.sendCount > 0);
    BOOST_CHECK(mock2.sendCount > 0);
}

// ---------------------------------------------------------------------------
// Test: fallAsleep failure sets failed state
// ---------------------------------------------------------------------------
BOOST_FIXTURE_TEST_CASE(FallAsleepFailure, D2FlasherFixture)
{
    mock1.failOnPeriodic = true;
    mock2.failOnPeriodic = true;

    D2FlasherImpl impl(channels, common::CarPlatform::P2, 0x7A, emptyVbf,
        [](FlasherState) {}, [](size_t) {},
        [](ICanChannel&, uint8_t) {}, [](ICanChannel&, uint8_t) {});

    impl.fallAsleep();

    BOOST_CHECK(impl.isFailed());
}

// ---------------------------------------------------------------------------
// Test: Successful fallAsleep
// ---------------------------------------------------------------------------
BOOST_FIXTURE_TEST_CASE(FallAsleepSuccess, D2FlasherFixture)
{
    D2FlasherImpl impl(channels, common::CarPlatform::P2, 0x7A, emptyVbf,
        [](FlasherState) {}, [](size_t) {},
        [](ICanChannel&, uint8_t) {}, [](ICanChannel&, uint8_t) {});

    impl.fallAsleep();

    BOOST_CHECK(!impl.isFailed());
}

// ---------------------------------------------------------------------------
// Test: eraseFlash calls erase callback
// ---------------------------------------------------------------------------
BOOST_FIXTURE_TEST_CASE(EraseFlashCallsCallback, D2FlasherFixture)
{
    bool eraseCalled = false;
    D2FlasherImpl impl(channels, common::CarPlatform::P2, 0x7A, emptyVbf,
        [](FlasherState) {}, [](size_t) {},
        [&](ICanChannel&, uint8_t) { eraseCalled = true; },
        [](ICanChannel&, uint8_t) {});

    impl.eraseFlash();

    BOOST_CHECK(eraseCalled);
    BOOST_CHECK(!impl.isFailed());
}

// ---------------------------------------------------------------------------
// Test: eraseFlash callback throw sets failed
// ---------------------------------------------------------------------------
BOOST_FIXTURE_TEST_CASE(EraseFlashThrow, D2FlasherFixture)
{
    D2FlasherImpl impl(channels, common::CarPlatform::P2, 0x7A, emptyVbf,
        [](FlasherState) {}, [](size_t) {},
        [](ICanChannel&, uint8_t) { throw std::runtime_error("fail"); },
        [](ICanChannel&, uint8_t) {});

    impl.eraseFlash();

    BOOST_CHECK(impl.isFailed());
}

// ---------------------------------------------------------------------------
// Test: writeFlash calls write callback
// ---------------------------------------------------------------------------
BOOST_FIXTURE_TEST_CASE(WriteFlashCallsCallback, D2FlasherFixture)
{
    bool writeCalled = false;
    D2FlasherImpl impl(channels, common::CarPlatform::P2, 0x7A, emptyVbf,
        [](FlasherState) {}, [](size_t) {},
        [](ICanChannel&, uint8_t) {},
        [&](ICanChannel&, uint8_t) { writeCalled = true; });

    impl.writeFlash();

    BOOST_CHECK(writeCalled);
    BOOST_CHECK(!impl.isFailed());
}

// ---------------------------------------------------------------------------
// Test: writeFlash callback throw sets failed
// ---------------------------------------------------------------------------
BOOST_FIXTURE_TEST_CASE(WriteFlashThrow, D2FlasherFixture)
{
    D2FlasherImpl impl(channels, common::CarPlatform::P2, 0x7A, emptyVbf,
        [](FlasherState) {}, [](size_t) {},
        [](ICanChannel&, uint8_t) {},
        [](ICanChannel&, uint8_t) { throw std::runtime_error("fail"); });

    impl.writeFlash();

    BOOST_CHECK(impl.isFailed());
}

// ---------------------------------------------------------------------------
// Test: done sets Done state via callback
// ---------------------------------------------------------------------------
BOOST_FIXTURE_TEST_CASE(DoneState, D2FlasherFixture)
{
    FlasherState recordedState = FlasherState::Initial;
    D2FlasherImpl impl(channels, common::CarPlatform::P2, 0x7A, emptyVbf,
        [&](FlasherState s) { recordedState = s; }, [](size_t) {},
        [](ICanChannel&, uint8_t) {}, [](ICanChannel&, uint8_t) {});

    impl.done();

    BOOST_CHECK(recordedState == FlasherState::Done);
}

// ---------------------------------------------------------------------------
// Test: error sets Error state via callback
// ---------------------------------------------------------------------------
BOOST_FIXTURE_TEST_CASE(ErrorState, D2FlasherFixture)
{
    FlasherState recordedState = FlasherState::Initial;
    D2FlasherImpl impl(channels, common::CarPlatform::P2, 0x7A, emptyVbf,
        [&](FlasherState s) { recordedState = s; }, [](size_t) {},
        [](ICanChannel&, uint8_t) {}, [](ICanChannel&, uint8_t) {});

    impl.error();

    BOOST_CHECK(recordedState == FlasherState::Error);
}

// ---------------------------------------------------------------------------
// Test: progress updates
// ---------------------------------------------------------------------------
BOOST_FIXTURE_TEST_CASE(ProgressUpdates, D2FlasherFixture)
{
    size_t recordedProgress = 0;
    D2FlasherImpl impl(channels, common::CarPlatform::P2, 0x7A, emptyVbf,
        [](FlasherState) {},
        [&](size_t p) { recordedProgress += p; },
        [](ICanChannel&, uint8_t) {}, [](ICanChannel&, uint8_t) {});

    impl.setMaximumFlashProgressValue(200);
    auto maxProgress = impl.getMaximumProgress();

    BOOST_CHECK(maxProgress > 0);
}
