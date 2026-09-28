/*****************************************************************************
 * Copyright (c) 2014-2026 OpenRCT2 developers. GPL-3.0-or-later.
 *****************************************************************************/
#include <gtest/gtest.h>
#include <openrct2/Context.h>
#include <openrct2/GameState.h>
#include <openrct2/OpenRCT2.h>
#include <openrct2/ParkImporter.h>
#include <openrct2/PlatformEnvironment.h>
#include <openrct2/audio/AudioContext.h>
#include <openrct2/core/MemoryStream.h>
#include <openrct2/drawing/Palette.h>
#include <openrct2/drawing/RenderService.h>
#include <openrct2/drawing/RenderTarget.h>
#include <openrct2/park/ParkFile.h>
#include <openrct2/park/ParkPreview.h>
#include <openrct2/ui/UiContext.h>
#include <openrct2/world/Map.h>

#include <cstring>

using namespace OpenRCT2;
using namespace OpenRCT2::Drawing;

namespace
{
    struct PreviewState
    {
        bool enabled{ true };
        bool nullSession{};
        bool malformedTarget{};
        size_t creations{};
        std::vector<OffscreenRenderRequest> requests;
    };

    class MalformedPreviewSession final : public IRenderSession
    {
        RenderTarget _target{};

    public:
        IDrawingContext& GetDrawingContext() override { throw std::logic_error("Malformed target must not be painted"); }
        RenderTarget& GetRenderTarget() override { return _target; }
        std::shared_ptr<IRenderCompletion> Submit() override
        {
            throw std::logic_error("Malformed target must not be submitted");
        }
        void Cancel() noexcept override {}
    };

    class FailingPreviewService final : public IRenderService
    {
        PreviewState& _state;

    public:
        explicit FailingPreviewService(PreviewState& state) : _state(state) {}
        std::unique_ptr<IRenderSession> BeginOffscreen(OffscreenRenderRequest request) override
        {
            _state.requests.push_back(std::move(request));
            if (_state.nullSession)
                return {};
            if (_state.malformedTarget)
                return std::make_unique<MalformedPreviewSession>();
            throw RenderServiceException({ RenderErrorCode::deviceLost, "Synthetic park preview device loss" });
        }
        void Shutdown() noexcept override {}
    };

    class PreviewFactory final : public IRenderServiceFactory
    {
        PreviewState& _state;

    public:
        explicit PreviewFactory(PreviewState& state) : _state(state) {}
        bool IsEnabled() const override { return _state.enabled; }
        std::unique_ptr<IRenderService> Create() override
        {
            ++_state.creations;
            return std::make_unique<FailingPreviewService>(_state);
        }
    };

    struct RestorePreviewGlobals
    {
        bool headless{ gOpenRCT2Headless };
        bool noGraphics{ gOpenRCT2NoGraphics };
        GamePalette palette{ gPalette };
        ~RestorePreviewGlobals()
        {
            gOpenRCT2Headless = headless;
            gOpenRCT2NoGraphics = noGraphics;
            gPalette = palette;
        }
    };

    class ParkPreviewTest : public testing::Test
    {
    protected:
        RestorePreviewGlobals _restore;
        PreviewState _state;
        std::unique_ptr<IContext> _context;

        void SetUp() override
        {
            gOpenRCT2Headless = true;
            gOpenRCT2NoGraphics = true;
            _context = CreateContext(
                CreatePlatformEnvironment(), Audio::CreateDummyAudioContext(), Ui::CreateDummyUiContext(),
                std::make_shared<PreviewFactory>(_state));
            ASSERT_TRUE(_context->Initialise());
            MapInit({ 16, 16 });
            getGameState().park.name = "Preview failure must not lose this park";
            getGameState().park.entrances = { CoordsXYZD{ 128, 128, 16, 0 } };
        }

        void TearDown() override { _context.reset(); }
    };
}

TEST_F(ParkPreviewTest, NoGraphicsAndMissingCameraDoNotAcquireADevice)
{
    auto preview = generatePreviewFromGameState(getGameState());
    ASSERT_EQ(preview.images.size(), 1u);
    EXPECT_EQ(preview.images[0].type, PreviewImageType::miniMap);
    EXPECT_EQ(_state.creations, 0u);

    gOpenRCT2NoGraphics = false;
    getGameState().park.entrances.clear();
    preview = generatePreviewFromGameState(getGameState());
    ASSERT_EQ(preview.images.size(), 1u);
    EXPECT_EQ(_state.creations, 0u);
}

TEST_F(ParkPreviewTest, UnavailableScreenshotPreservesMetadataAndMinimapWithoutSoftwareFallback)
{
    const auto expected = generatePreviewFromGameState(getGameState());
    gOpenRCT2NoGraphics = false;
    _state.enabled = false;
    const auto preview = generatePreviewFromGameState(getGameState());
    EXPECT_EQ(preview.parkName, expected.parkName);
    EXPECT_EQ(preview.parkRating, expected.parkRating);
    ASSERT_EQ(preview.images.size(), 1u);
    EXPECT_EQ(preview.images[0].type, PreviewImageType::miniMap);
    EXPECT_EQ(std::memcmp(preview.images[0].pixels, expected.images[0].pixels, sizeof(preview.images[0].pixels)), 0);
    EXPECT_EQ(_state.creations, 0u);
}

TEST_F(ParkPreviewTest, DeviceFailureStillExportsReadableParkWithMinimap)
{
    gOpenRCT2NoGraphics = false;
    for (size_t i = 0; i < gPalette.size(); ++i)
        gPalette[i] = { static_cast<uint8_t>(i), 31, 97, 255 };
    MemoryStream stream;
    ParkFileExporter exporter;
    ASSERT_NO_THROW(exporter.Export(getGameState(), stream, kParkFileSaveCompressionLevel));
    ASSERT_EQ(_state.creations, 1u);
    ASSERT_EQ(_state.requests.size(), 1u);
    const auto& request = _state.requests[0];
    EXPECT_EQ(request.logicalExtent, (RenderExtent{ 250, 200 }));
    EXPECT_EQ(request.outputExtent, request.logicalExtent);
    EXPECT_TRUE(request.indexedOutput);
    EXPECT_FALSE(request.rgbaOutput);
    EXPECT_FALSE(request.lightingEnabled);
    EXPECT_EQ(request.initialContents, RenderInitialContents::clearIndex);
    EXPECT_EQ(request.clearIndex, 0u);
    for (size_t i = 0; i < request.palette.size(); ++i)
    {
        EXPECT_EQ(request.palette[i].red, 97);
        EXPECT_EQ(request.palette[i].green, 31);
        EXPECT_EQ(request.palette[i].blue, i);
    }
    stream.SetPosition(0);
    auto importer = ParkImporter::CreateParkFile(_context->GetObjectRepository());
    ASSERT_NO_THROW(static_cast<void>(importer->LoadFromStream(&stream, false)));
    const auto saved = importer->GetParkPreview();
    EXPECT_EQ(saved.parkName, getGameState().park.name);
    ASSERT_EQ(saved.images.size(), 1u);
    EXPECT_EQ(saved.images[0].type, PreviewImageType::miniMap);
}

TEST_F(ParkPreviewTest, MissingSessionAndMalformedTargetOmitOnlyScreenshot)
{
    gOpenRCT2NoGraphics = false;
    _state.nullSession = true;
    auto preview = generatePreviewFromGameState(getGameState());
    ASSERT_EQ(preview.images.size(), 1u);
    EXPECT_EQ(preview.images[0].type, PreviewImageType::miniMap);
    _state.nullSession = false;
    _state.malformedTarget = true;
    preview = generatePreviewFromGameState(getGameState());
    ASSERT_EQ(preview.images.size(), 1u);
    EXPECT_EQ(preview.images[0].type, PreviewImageType::miniMap);
    EXPECT_EQ(_state.creations, 1u);
    EXPECT_EQ(_state.requests.size(), 2u);
}
