/*****************************************************************************
 * Copyright (c) 2014-2026 OpenRCT2 developers. GPL-3.0-or-later.
 *****************************************************************************/
#include <gtest/gtest.h>
#include <openrct2/Context.h>
#include <openrct2/GameState.h>
#include <openrct2/OpenRCT2.h>
#include <openrct2/PlatformEnvironment.h>
#include <openrct2/audio/AudioContext.h>
#include <openrct2/config/Config.h>
#include <openrct2/drawing/IDrawingContext.h>
#include <openrct2/drawing/IDrawingEngine.h>
#include <openrct2/drawing/PaletteIndex.h>
#include <openrct2/drawing/RenderService.h>
#include <openrct2/drawing/RenderTarget.h>
#include <openrct2/object/ObjectManager.h>
#include <openrct2/ride/Ride.h>
#include <openrct2/ride/RideConstruction.h>
#include <openrct2/ride/RideManager.hpp>
#include <openrct2/ride/TrackDesign.h>
#include <openrct2/ui/UiContext.h>
#include <openrct2/world/Map.h>
#include <openrct2/world/MapPresentationSnapshot.h>
#ifdef ENABLE_SCRIPTING
    #include <openrct2/scripting/ScriptEngine.h>
#endif

#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <string_view>

using namespace OpenRCT2;
using namespace OpenRCT2::Drawing;

namespace
{
    enum class FailurePoint { create, begin, target, secondSubmit, secondWait };

    struct FailureState
    {
        FailurePoint point{};
        size_t creations{};
        size_t sessionsDestroyed{};
        size_t temporaryRideCount{};
        size_t submits{};
        size_t waits{};
        size_t successfulResults{};
        TileCoordsXY temporaryMapSize{};
        uint64_t temporaryMapEpoch{};
        std::vector<OffscreenRenderRequest> requests;

        void ObserveTemporaryWorld()
        {
            temporaryMapSize = getGameState().mapSize;
            temporaryMapEpoch = GetMapPresentationEpoch();
            temporaryRideCount = RideManager(getGameState()).size();
        }
        [[noreturn]] static void Fail()
        {
            throw RenderServiceException({ RenderErrorCode::deviceLost, "Injected track preview failure" });
        }
    };

    // This test verifies transaction/lifetime behavior, not raster output. A valid first
    // rotation returns a distinct sentinel image before the second rotation fails.
    class PreviewProbeEngine final : public IDrawingEngine, public IDrawingContext
    {
        RenderTarget& _target;

    public:
        explicit PreviewProbeEngine(RenderTarget& target) : _target(target) {}
        void Initialise() override {}
        void Resize(uint32_t, uint32_t) override {}
        void SetPalette(const GamePalette&) override {}
        void SetVSync(bool) override {}
        void Invalidate(int32_t, int32_t, int32_t, int32_t) override {}
        void BeginDraw() override {}
        void EndDraw() override {}
        void PaintWindows() override {}
        void PaintWeather() override {}
        void CopyRect(int32_t, int32_t, int32_t, int32_t, int32_t, int32_t) override {}
        std::string Screenshot() override { return {}; }
        IDrawingContext* GetDrawingContext() override { return this; }
        RenderTarget* getRT() override { return &_target; }
        DrawingEngineFlags GetFlags() override { return {}; }
        void InvalidateImage(uint32_t) override {}
        void Clear(RenderTarget&, PaletteIndex) override {}
        void FillRect(RenderTarget&, PaletteIndex, int32_t, int32_t, int32_t, int32_t, bool) override {}
        void FilterRect(RenderTarget&, FilterPaletteID, int32_t, int32_t, int32_t, int32_t) override {}
        void DrawLine(RenderTarget&, PaletteIndex, const ScreenLine&) override {}
        void DrawSprite(RenderTarget&, ImageId, int32_t, int32_t) override {}
        void DrawSpriteRawMasked(RenderTarget&, int32_t, int32_t, ImageId, ImageId) override {}
        void DrawSpriteSolid(RenderTarget&, ImageId, int32_t, int32_t, PaletteIndex) override {}
        void DrawGlyph(RenderTarget&, ImageId, int32_t, int32_t, const PaletteMap&) override {}
        void DrawTTFBitmap(RenderTarget&, const TextDrawInfo&, TTFSurface*, int32_t, int32_t, uint8_t) override {}
    };

    class ProgressCompletion final : public IRenderCompletion
    {
        FailureState& _state;
        const OffscreenRenderRequest _request;
        const RenderSubmissionIdentity _identity;
        const bool _fail;

    public:
        ProgressCompletion(FailureState& state, OffscreenRenderRequest request, uint64_t ordinal, bool fail)
            : _state(state), _request(std::move(request)),
              _identity{ ordinal, ordinal, 1, _request.name }, _fail(fail) {}
        const RenderSubmissionIdentity& GetIdentity() const noexcept override { return _identity; }
        void Cancel() override {}
        RenderOutcome Wait(std::chrono::milliseconds) override
        {
            ++_state.waits;
            if (_fail)
                return { _identity, {}, RenderError{ RenderErrorCode::deviceLost, "Injected second rotation wait failure" } };
            auto result = std::make_shared<RenderResult>();
            result->identity = _identity;
            result->logicalExtent = _request.logicalExtent;
            result->outputExtent = _request.outputExtent;
            result->palette = _request.palette;
            result->indexed.assign(kTrackPreviewImageSize, std::byte{ 42 });
            ++_state.successfulResults;
            return { _identity, std::move(result), {} };
        }
    };

    class ProgressSession final : public IRenderSession
    {
        FailureState& _state;
        OffscreenRenderRequest _request;
        uint64_t _ordinal;
        std::vector<PaletteIndex> _bits;
        RenderTarget _target{};
        PreviewProbeEngine _engine;

    public:
        ProgressSession(FailureState& state, OffscreenRenderRequest request, uint64_t ordinal)
            : _state(state), _request(std::move(request)), _ordinal(ordinal),
              _bits(kTrackPreviewImageSize), _engine(_target)
        {
            _target.bits = _bits.data();
            _target.width = 370;
            _target.height = 217;
            _target.DrawingEngine = &_engine;
        }
        ~ProgressSession() override { ++_state.sessionsDestroyed; }
        IDrawingContext& GetDrawingContext() override { return _engine; }
        RenderTarget& GetRenderTarget() override { return _target; }
        void Cancel() noexcept override {}
        std::shared_ptr<IRenderCompletion> Submit() override
        {
            ++_state.submits;
            if (_ordinal == 2 && _state.point == FailurePoint::secondSubmit)
                FailureState::Fail();
            return std::make_shared<ProgressCompletion>(
                _state, _request, _ordinal, _ordinal == 2 && _state.point == FailurePoint::secondWait);
        }
    };

    class FailingSession final : public IRenderSession
    {
        FailureState& _state;

    public:
        explicit FailingSession(FailureState& state) : _state(state) {}
        ~FailingSession() override { ++_state.sessionsDestroyed; }
        IDrawingContext& GetDrawingContext() override { FailureState::Fail(); }
        RenderTarget& GetRenderTarget() override { FailureState::Fail(); }
        std::shared_ptr<IRenderCompletion> Submit() override { FailureState::Fail(); }
        void Cancel() noexcept override {}
    };

    class FailingService final : public IRenderService
    {
        FailureState& _state;

    public:
        explicit FailingService(FailureState& state) : _state(state) {}
        std::unique_ptr<IRenderSession> BeginOffscreen(OffscreenRenderRequest request) override
        {
            _state.ObserveTemporaryWorld();
            _state.requests.push_back(std::move(request));
            if (_state.point == FailurePoint::begin)
                FailureState::Fail();
            if (_state.point == FailurePoint::secondSubmit || _state.point == FailurePoint::secondWait)
                return std::make_unique<ProgressSession>(_state, _state.requests.back(), _state.requests.size());
            return std::make_unique<FailingSession>(_state);
        }
        void Shutdown() noexcept override {}
    };

    class FailingFactory final : public IRenderServiceFactory
    {
        FailureState& _state;

    public:
        explicit FailingFactory(FailureState& state) : _state(state) {}
        std::unique_ptr<IRenderService> Create() override
        {
            ++_state.creations;
            _state.ObserveTemporaryWorld();
            if (_state.point == FailurePoint::create)
                FailureState::Fail();
            return std::make_unique<FailingService>(_state);
        }
    };

    class TrackDesignPreviewTest : public testing::TestWithParam<FailurePoint>
    {
    protected:
        bool _oldHeadless{ gOpenRCT2Headless };
        bool _oldNoGraphics{ gOpenRCT2NoGraphics };
        LegacyScene _oldScene{ gLegacyScene };
        uint8_t _oldDirection{ _currentTrackPieceDirection };
        RideId _oldRide{ _currentRideIndex };
        bool _oldPreview{ _trackDesignDrawingPreview };
        std::string _oldRct1{ Config::Get().general.rct1Path };
        std::string _oldRct2{ Config::Get().general.rct2Path };
        bool _contextReady{};
        FailureState _failure;
        std::unique_ptr<IContext> _context;
        TrackDesign _design;

        void SetUp() override
        {
            const bool paintsViewport = GetParam() == FailurePoint::secondSubmit || GetParam() == FailurePoint::secondWait;
            const auto* rct2 = std::getenv("OPENRCT2_TEST_RCT2_PATH");
            if (paintsViewport && (rct2 == nullptr || *rct2 == '\0'))
            {
                const auto* required = std::getenv("OPENRCT2_REQUIRE_VULKAN_TESTS");
                if (required != nullptr && std::string_view(required) == "1")
                    FAIL() << "Late track preview failures require pinned OPENRCT2_TEST_RCT2_PATH";
                GTEST_SKIP() << "Late track preview failures require OPENRCT2_TEST_RCT2_PATH";
            }
            gOpenRCT2Headless = true;
            // Late failures traverse real viewport/track paint before the probe's Submit/Wait.
            // Load its sprite metadata while retaining the CPU-only injected service and no window.
            gOpenRCT2NoGraphics = !paintsViewport;
            gLegacyScene = LegacyScene::playing;
            _context = CreateContext(
                CreatePlatformEnvironment(), Audio::CreateDummyAudioContext(), Ui::CreateDummyUiContext(),
                std::make_shared<FailingFactory>(_failure));
            if (paintsViewport)
            {
                auto& environment = _context->GetPlatformEnvironment();
                Config::Get().general.rct2Path = rct2;
                environment.SetBasePath(DirBase::rct2, rct2);
                if (const auto* rct1 = std::getenv("OPENRCT2_TEST_RCT1_PATH"))
                {
                    Config::Get().general.rct1Path = rct1;
                    environment.SetBasePath(DirBase::rct1, rct1);
                }
                auto data = std::filesystem::current_path() / "data";
                if (const auto* shaders = std::getenv("OPENRCT2_VULKAN_SHADER_DIRECTORY"))
                    data = std::filesystem::path(shaders).parent_path().parent_path();
                else if (!std::filesystem::is_regular_file(data / "g2.dat"))
                    data = std::filesystem::current_path() / "bin/data";
                environment.SetBasePath(DirBase::openrct2, data.string());
            }
            _contextReady = _context->Initialise();
            ASSERT_TRUE(_contextReady);
            // Context construction and MapInit alone preserve rides from preceding tests.
            gameStateInitAll(getGameState(), { 16, 16 });
            ASSERT_EQ(RideManager(getGameState()).size(), 0u);
            auto& manager = _context->GetObjectManager();
            ASSERT_NE(manager.LoadObject("rct2.ride.spboat"), nullptr);
            _design.trackAndVehicle.rtdIndex = RIDE_TYPE_SPLASH_BOATS;
            _design.trackAndVehicle.vehicleObject = ObjectEntryDescriptor("rct2.ride.spboat");
            _design.trackElements.push_back({ TrackElemType::flat });
            _design.gameStateData.name = "Uncommitted preview";
            _design.gameStateData.cost = 123;
            _design.gameStateData.flags = 7;
            ASSERT_EQ(GetRide(RideId::FromUnderlying(5)), nullptr);
            auto* liveRide = RideAllocateAtIndex(RideId::FromUnderlying(5));
            ASSERT_NE(liveRide, nullptr);
            liveRide->type = RIDE_TYPE_SPLASH_BOATS;
            liveRide->customName = "Existing park ride";
            _currentTrackPieceDirection = 3;
            _currentRideIndex = liveRide->id;
            _trackDesignDrawingPreview = false;
            getGameState().park.flags.set(ParkFlag::forbidHighConstruction);
        }

        void TearDown() override
        {
#ifdef ENABLE_SCRIPTING
            if (_context != nullptr && !_contextReady)
            {
                // Failed asset startup can precede per-context script registration.
                try { _context->GetScriptEngine().Initialise(); }
                catch (const std::runtime_error& error)
                {
                    EXPECT_STREQ(error.what(), "Script engine already initialised.");
                }
            }
#endif
            _context.reset();
            _currentTrackPieceDirection = _oldDirection;
            _currentRideIndex = _oldRide;
            _trackDesignDrawingPreview = _oldPreview;
            gLegacyScene = _oldScene;
            gOpenRCT2Headless = _oldHeadless;
            gOpenRCT2NoGraphics = _oldNoGraphics;
            Config::Get().general.rct1Path = _oldRct1;
            Config::Get().general.rct2Path = _oldRct2;
        }
    };
}

TEST_P(TrackDesignPreviewTest, ServiceFailureRestoresLiveWorldAndPendingPublicationWithoutPartialOutput)
{
    _failure.point = GetParam();
    auto& state = getGameState();
    const auto tiles = state.tileElements;
    const auto* firstTile = MapGetFirstElementAt(TileCoordsXY{ 2, 2 });
    const auto revision = GetTileElementRevision({ 2, 2 });
    const auto epoch = GetMapPresentationEpoch();
    const auto flags = state.park.flags;
    const auto tick = state.currentTicks;
    const auto savedView = state.savedView;
    const auto savedZoom = state.savedViewZoom;
    const auto savedRotation = state.savedViewRotation;
    const auto initial = ConsumeMapPresentationChanges();
    ASSERT_TRUE(initial.reset);
    MapInvalidateTileFull({ 64, 64 });
    auto pixels = std::make_unique<TrackDesignPreviewBuffer>();
    pixels->fill(PaletteIndex::pi17);

    EXPECT_THROW(TrackDesignDrawPreview(_design, *pixels, false), RenderServiceException);
    EXPECT_EQ(_failure.creations, 1u);
    EXPECT_EQ(_failure.temporaryMapSize, (TileCoordsXY{ 256, 256 }));
    EXPECT_NE(_failure.temporaryMapEpoch, epoch);
    EXPECT_EQ(_failure.temporaryRideCount, 2u);
    EXPECT_EQ(state.mapSize, (TileCoordsXY{ 16, 16 }));
    ASSERT_EQ(state.tileElements.size(), tiles.size());
    EXPECT_EQ(std::memcmp(state.tileElements.data(), tiles.data(), tiles.size() * sizeof(TileElement)), 0);
    EXPECT_EQ(MapGetFirstElementAt(TileCoordsXY{ 2, 2 }), firstTile);
    EXPECT_EQ(GetTileElementRevision({ 2, 2 }), revision);
    EXPECT_EQ(GetMapPresentationEpoch(), epoch);
    EXPECT_EQ(RideManager(state).size(), 1u);
    ASSERT_NE(GetRide(RideId::FromUnderlying(5)), nullptr);
    EXPECT_EQ(GetRide(RideId::FromUnderlying(5))->customName, "Existing park ride");
    EXPECT_EQ(_currentRideIndex, RideId::FromUnderlying(5));
    EXPECT_EQ(_currentTrackPieceDirection, 3);
    EXPECT_FALSE(_trackDesignDrawingPreview);
    EXPECT_EQ(state.park.flags, flags);
    EXPECT_EQ(state.currentTicks, tick);
    EXPECT_EQ(state.savedView, savedView);
    EXPECT_EQ(state.savedViewZoom, savedZoom);
    EXPECT_EQ(state.savedViewRotation, savedRotation);
    EXPECT_EQ(_design.gameStateData.name, "Uncommitted preview");
    EXPECT_EQ(_design.gameStateData.cost, 123);
    EXPECT_EQ(_design.gameStateData.flags, 7);
    EXPECT_TRUE(std::all_of(pixels->begin(), pixels->end(), [](auto index) { return index == PaletteIndex::pi17; }));
    const auto changes = ConsumeMapPresentationChanges();
    EXPECT_FALSE(changes.reset);
    EXPECT_EQ(changes.epoch, epoch);
    ASSERT_EQ(changes.changes.size(), 1u);
    EXPECT_EQ(changes.changes[0].index, 2u + 2u * kMaximumMapSizeTechnical);
    const bool lateFailure = GetParam() == FailurePoint::secondSubmit || GetParam() == FailurePoint::secondWait;
    EXPECT_EQ(_failure.sessionsDestroyed, lateFailure ? 2u : GetParam() == FailurePoint::target ? 1u : 0u);
    if (lateFailure)
    {
        ASSERT_EQ(_failure.requests.size(), 2u);
        EXPECT_EQ(_failure.requests[0].name, "track-design-preview-0");
        EXPECT_EQ(_failure.requests[1].name, "track-design-preview-1");
        EXPECT_EQ(_failure.submits, 2u);
        EXPECT_EQ(_failure.waits, GetParam() == FailurePoint::secondWait ? 2u : 1u);
        EXPECT_EQ(_failure.successfulResults, 1u);
    }
}

INSTANTIATE_TEST_SUITE_P(
    InjectedFailures, TrackDesignPreviewTest,
    testing::Values(FailurePoint::create, FailurePoint::begin, FailurePoint::target,
                    FailurePoint::secondSubmit, FailurePoint::secondWait));
