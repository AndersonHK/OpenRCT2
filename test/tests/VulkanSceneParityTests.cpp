/*****************************************************************************
 * Copyright (c) 2014-2026 OpenRCT2 developers
 *
 * OpenRCT2 is licensed under the GNU General Public License version 3.
 *****************************************************************************/

#include <cstdlib>
#include <gtest/gtest.h>
#include <string_view>

namespace
{
    bool RequiredSceneParity()
    {
        const auto* value = std::getenv("OPENRCT2_REQUIRE_VULKAN_TESTS");
        return value != nullptr && std::string_view(value) == "1";
    }
} // namespace

#ifdef ENABLE_VULKAN

    #include "TestData.h"
    #include "VulkanParityTestSupport.h"

    #include <SDL.h>
    #include <openrct2-renderer/gpu/GpuCommandDrawingContext.h>
    #include <openrct2-renderer/vulkan/VulkanBackend.h>
    #include <openrct2-ui/drawing/engines/vulkan/VulkanPlatform.h>
    #include <openrct2/Context.h>
    #include <openrct2/Game.h>
    #include <openrct2/GameState.h>
    #include <openrct2/OpenRCT2.h>
    #include <openrct2/ParkImporter.h>
    #include <openrct2/PlatformEnvironment.h>
    #include <openrct2/SpriteIds.h>
    #include <openrct2/audio/AudioContext.h>
    #include <openrct2/config/Config.h>
    #include <openrct2/core/JobPool.h>
    #include <openrct2/drawing/BlendColourMap.h>
    #include <openrct2/drawing/Drawing.Sprite.h>
    #include <openrct2/drawing/Drawing.h>
    #include <openrct2/drawing/Font.h>
    #include <openrct2/drawing/Palette.h>
    #include <openrct2/drawing/SpriteAssetDecoder.h>
    #include <openrct2/drawing/X8DrawingEngine.h>
    #include <openrct2/entity/Balloon.h>
    #include <openrct2/entity/EntityRegistry.h>
    #include <openrct2/entity/EntityTweener.h>
    #include <openrct2/interface/Viewport.h>
    #include <openrct2/object/ObjectManager.h>
    #include <openrct2/object/PeepAnimationsObject.h>
    #include <openrct2/rct1/Csg.h>
    #include <openrct2/ride/Ride.h>
    #include <openrct2/ride/RideEntry.h>
    #include <openrct2/world/Map.h>
    #include <openrct2/world/MapAnimation.h>
    #include <openrct2/world/MapSelection.h>
    #include <openrct2/world/Weather.h>
    #include <openrct2/ui/UiContext.h>

namespace
{
    namespace Gpu = OpenRCT2::Ui::Gpu;
    namespace Vulkan = OpenRCT2::Ui::Vulkan;
    using namespace OpenRCT2;
    using namespace OpenRCT2::Drawing;
    using namespace VulkanParitySupport;

    constexpr Gpu::Extent kSceneExtent{ 640, 480 };
    constexpr size_t kScenePixels = kSceneExtent.width * kSceneExtent.height;

    InferredSpriteAssetBounds FrozenLoadedBounds(ImageIndex first, uint32_t count)
    {
        std::array<PaletteIndex, 200 * 200> pixels{};
        RenderTarget target{};
        target.bits = pixels.data();
        target.x = -100;
        target.y = -100;
        target.width = 200;
        target.height = 200;
        for (uint32_t i = 0; i < count; i++)
            GfxDrawSpriteSoftware(target, ImageId(first + i), { 0, 0 });
        InferredSpriteAssetBounds bounds{};
        for (int32_t distance = 99; distance != 0; distance--)
        {
            for (int32_t across = 0; across < 200; across++)
            {
                if (bounds.width == 0
                    && (pixels[across * 200 + 100 - distance] != PaletteIndex::transparent
                        || pixels[across * 200 + 100 + distance] != PaletteIndex::transparent))
                    bounds.width = static_cast<uint8_t>(distance + 1);
                if (bounds.heightNegative == 0 && pixels[(100 - distance) * 200 + across] != PaletteIndex::transparent)
                    bounds.heightNegative = static_cast<uint8_t>(distance + 1);
                if (bounds.heightPositive == 0 && pixels[(100 + distance) * 200 + across] != PaletteIndex::transparent)
                    bounds.heightPositive = static_cast<uint8_t>(distance + 1);
            }
        }
        return bounds;
    }

    void ValidateLoadedAssetMetadata(const Ride& ride, json_t& summary)
    {
        for (uint32_t glyph = 0; glyph < SPR_FONTS_GLYPH_COUNT; glyph++)
        {
            const auto image = SPR_FONTS_BEGIN + EnumValue(FontStyle::tiny) * SPR_FONTS_GLYPH_COUNT + glyph;
            SCOPED_TRACE(::testing::Message() << "loaded tiny glyph " << glyph);
            const auto* element = GfxGetG1Element(image);
            ASSERT_NE(element, nullptr);
            std::array<PaletteIndex, 64> pixels{};
            RenderTarget target{};
            target.bits = pixels.data();
            target.width = 8;
            target.height = 8;
            GfxDrawSpriteSoftware(target, ImageId(image), { -1, 0 });
            const auto mask = ExtractScrollingGlyphMask(*element);
            for (size_t x = 0; x < 8; x++)
                for (size_t y = 0; y < 8; y++)
                    EXPECT_EQ((mask[x] & (1U << y)) != 0, pixels[y * 8 + x] == PaletteIndex::fontFill);
        }
        summary["tinyGlyphs"] = SPR_FONTS_GLYPH_COUNT;
        const auto* entry = ride.getRideEntry();
        ASSERT_NE(entry, nullptr);
        size_t cars = 0;
        for (auto car : entry->Cars)
        {
            if (car.numCarImages == 0)
                continue;
            auto count = car.numCarImages * (car.numSeatingRows + 1);
            const bool inverted = car.flags.has(CarEntryFlag::spriteBoundsIncludeInvertedSet);
            if (inverted)
                count *= 2;
            const auto expected = FrozenLoadedBounds(car.baseImageId, count);
            CarEntrySetImageMaxSizes(car, static_cast<int32_t>(count));
            EXPECT_EQ(car.spriteWidth, expected.width);
            EXPECT_EQ(car.spriteHeightNegative, expected.heightNegative + (inverted ? 16 : 0));
            EXPECT_EQ(car.spriteHeightPositive, expected.heightPositive);
            cars++;
        }
        EXPECT_GT(cars, 0u);
        summary["loadedRideCars"] = cars;
        const auto* guest = findPeepAnimationsObjectForType(AnimationPeepType::guest);
        ASSERT_NE(guest, nullptr);
        for (size_t group = 0; group < guest->GetNumAnimationGroups(); group++)
        {
            const auto& animation = guest->GetPeepAnimation(static_cast<PeepAnimationGroup>(group));
            ASSERT_FALSE(animation.frameOffsets.empty());
            const auto expected = FrozenLoadedBounds(
                animation.baseImage, *std::max_element(animation.frameOffsets.begin(), animation.frameOffsets.end()) + 1);
            const auto actual = inferMaxAnimationDimensions(animation);
            EXPECT_EQ(actual.spriteWidth, expected.width);
            EXPECT_EQ(actual.spriteHeightNegative, expected.heightNegative);
            EXPECT_EQ(actual.spriteHeightPositive, expected.heightPositive);
        }
        summary["guestWalkingGroups"] = guest->GetNumAnimationGroups();
        // Asset-area census only: runtime atlas residency and packing costs must be measured separately.
        for (const auto [first, end] :
             { std::pair{ 0U, static_cast<uint32_t>(SPR_G1_END) },
               std::pair{ static_cast<uint32_t>(SPR_G2_BEGIN), static_cast<uint32_t>(SPR_G2_END) },
               std::pair{ static_cast<uint32_t>(SPR_FONTS_BEGIN), static_cast<uint32_t>(SPR_FONTS_END) } })
        {
            for (auto id = first; id < end; id++)
            {
                const auto* element = GfxGetG1Element(id);
                if (element == nullptr || element->flags.has(G1Flag::isPalette))
                    continue;
                const auto decoded = DecodeTrustedSpriteAsset(*element);
                auto& bucket = summary["coverageCensus"][element->flags.has(G1Flag::hasRLECompression) ? "rle" : "bitmap"];
                if (bucket.is_null())
                    bucket = json_t::object();
                bucket["assets"] = bucket.value("assets", uint64_t{}) + 1;
                bucket["decodedArea"] = bucket.value("decodedArea", uint64_t{}) + decoded.pixels.size();
                bool coveredZero = false;
                for (size_t i = 0; i < decoded.pixels.size(); i++)
                    coveredZero |= decoded.coverage[i] != 0 && decoded.pixels[i] == PaletteIndex::transparent;
                if (coveredZero)
                {
                    bucket["coveredZeroAssets"] = bucket.value("coveredZeroAssets", uint64_t{}) + 1;
                    bucket["coveredZeroArea"] = bucket.value("coveredZeroArea", uint64_t{}) + decoded.pixels.size();
                }
            }
        }
    }

    // Only routes auxiliary paint calls. Neither adapter is installed as the context's main engine, so
    // ViewportPaint reads the same live imported park and does not publish/consume the main presentation scene.
    class AuxiliaryEngine final : public IDrawingEngine
    {
        IDrawingContext& _context;
        RenderTarget& _target;

    public:
        AuxiliaryEngine(IDrawingContext& context, RenderTarget& target)
            : _context(context)
            , _target(target)
        {
            _target.DrawingEngine = this;
        }

        void Initialise() override
        {
        }
        void Resize(uint32_t, uint32_t) override
        {
        }
        void SetPalette(const GamePalette&) override
        {
        }
        void SetVSync(bool) override
        {
        }
        void Invalidate(int32_t, int32_t, int32_t, int32_t) override
        {
        }
        void BeginDraw() override
        {
        }
        void EndDraw() override
        {
        }
        void PaintWindows() override
        {
        }
        void PaintWeather() override
        {
        }
        void CopyRect(int32_t, int32_t, int32_t, int32_t, int32_t, int32_t) override
        {
        }
        std::string Screenshot() override
        {
            return {};
        }
        IDrawingContext* GetDrawingContext() override
        {
            return &_context;
        }
        RenderTarget* getRT() override
        {
            return &_target;
        }
        DrawingEngineFlags GetFlags() override
        {
            return {};
        }
        void InvalidateImage(uint32_t) override
        {
        }
    };

    uint64_t MapRevisionFingerprint()
    {
        const auto size = getGameState().mapSize;
        uint64_t result = 14695981039346656037ULL;
        for (int32_t y = 0; y < size.y; y++)
            for (int32_t x = 0; x < size.x; x++)
                result = (result ^ GetTileElementRevision({ x, y })) * 1099511628211ULL;
        return result;
    }

    std::array<std::byte, 1024> ScenePalette()
    {
        std::array<std::byte, 1024> result{};
        for (size_t i = 0; i < 256; i++)
        {
            result[i * 4] = static_cast<std::byte>(gPalette[i].red);
            result[i * 4 + 1] = static_cast<std::byte>(gPalette[i].green);
            result[i * 4 + 2] = static_cast<std::byte>(gPalette[i].blue);
            result[i * 4 + 3] = std::byte{ 255 };
        }
        return result;
    }

    std::array<std::byte, 256 * 256> SceneRemapTables()
    {
        std::array<std::byte, 256 * 256> result{};
        for (int32_t i = 0; i < 256; i++)
            result[i] = static_cast<std::byte>(i);
        for (int32_t i = 0; i < kPaletteTotalOffsets; i++)
        {
            const auto palette = static_cast<FilterPaletteID>(i);
            const auto image = GetPaletteG1Index(palette);
            if (!image.has_value())
                continue;
            const auto* element = GfxGetG1Element(*image);
            if (element == nullptr)
                continue;
            const auto decoded = DecodeTrustedSpriteAsset(*element);
            const auto row = Gpu::TextureCache::PaletteToY(palette);
            for (int32_t y = 0; y < std::min<int32_t>(element->height, 256 - row); y++)
                for (int32_t x = 0; x < std::min<int32_t>(element->width, 256); x++)
                {
                    const auto index = static_cast<size_t>(y) * element->width + x;
                    if (decoded.coverage[index] != 0)
                        result[(row + y) * 256 + x] = static_cast<std::byte>(decoded.pixels[index]);
                }
        }
        return result;
    }

    // Explicit legacy publication oracle, never a production dummy-context fallback.
    class LegacyPublicationOracleFactory final : public IDrawingEngineFactory
    {
        std::unique_ptr<IDrawingEngine> Create(Ui::IUiContext& ui) override
        {
            return std::make_unique<X8DrawingEngine>(ui);
        }
    };

    class VulkanSceneParityTest : public testing::Test
    {
    protected:
        const bool oldHeadless = gOpenRCT2Headless;
        const bool oldNoGraphics = gOpenRCT2NoGraphics;
        bool ownsVideo = false;
        std::unique_ptr<IContext> context;
        std::unique_ptr<SDL_Window, decltype(&SDL_DestroyWindow)> window{ nullptr, SDL_DestroyWindow };
        std::unique_ptr<Gpu::Backend> backend;
        std::filesystem::path shaders;
        std::filesystem::path parkPath;
        json_t inputHashes = json_t::object();

        std::string Initialise(bool legacyPublicationOracle = false)
        {
            const auto* rct2Path = std::getenv("OPENRCT2_TEST_RCT2_PATH");
            if (rct2Path == nullptr || *rct2Path == 0)
                return "Scene parity requires pinned OPENRCT2_TEST_RCT2_PATH";
            if (GetContext() != nullptr)
                return "Scene parity requires exclusive test context ownership";
            std::vector<std::filesystem::path> candidates{ std::filesystem::current_path() / "data/shaders/vulkan" };
            if (char* base = SDL_GetBasePath())
            {
                candidates.emplace_back(std::filesystem::path(base) / "data/shaders/vulkan");
                SDL_free(base);
            }
            if (const auto* path = std::getenv("OPENRCT2_VULKAN_SHADER_DIRECTORY"))
                candidates.insert(candidates.begin(), path);
            for (const auto& path : candidates)
                if (std::filesystem::is_regular_file(path / "indexed_palette.frag.spv"))
                {
                    shaders = path;
                    break;
                }
            if (shaders.empty())
                return "Compiled Vulkan shaders unavailable";

            gOpenRCT2Headless = true;
            gOpenRCT2NoGraphics = false;
            context = legacyPublicationOracle
                ? CreateContext(CreatePlatformEnvironment(), Audio::CreateDummyAudioContext(),
                    Ui::CreateDummyUiContext(std::make_shared<LegacyPublicationOracleFactory>()))
                : CreateContext();
            auto& env = context->GetPlatformEnvironment();
            env.SetBasePath(DirBase::rct2, rct2Path);
            if (const auto* rct1Path = std::getenv("OPENRCT2_TEST_RCT1_PATH"))
            {
                Config::Get().general.rct1Path = rct1Path;
                env.SetBasePath(DirBase::rct1, rct1Path);
            }
            const auto dataDirectory = shaders.parent_path().parent_path();
            env.SetBasePath(DirBase::openrct2, dataDirectory.string());
            const auto g1Path = env.FindFile(DirBase::rct2, DirId::data, "g1.dat");
            if (g1Path.empty() || !std::filesystem::is_regular_file(g1Path))
                return "Pinned original g1.dat unavailable";
            inputHashes["g1.dat"] = HashFile(g1Path);
            for (const auto* name : { "g2.dat", "palettes.dat", "fonts.dat", "tracks.dat" })
            {
                if (!std::filesystem::is_regular_file(dataDirectory / name))
                    return std::string("Bundled scene asset unavailable: ") + name;
                inputHashes[name] = HashFile(dataDirectory / name);
            }
            if (!context->Initialise())
                return "Headless graphics-enabled scene context failed to initialise";
            if (IsCsgLoaded())
            {
                inputHashes["csg1.dat"] = HashFile(FindCsg1datAtLocation(Config::Get().general.rct1Path));
                inputHashes["csg1i.dat"] = HashFile(FindCsg1idatAtLocation(Config::Get().general.rct1Path));
            }
            Config::Get().general.multiThreading = false;
            Config::Get().general.enableLightFx = false;
            Config::Get().general.landscapeSmoothing = true;

            parkPath = TestData::GetParkPath("small_park_with_ferris_wheel.sv6");
            inputHashes[parkPath.filename().string()] = HashFile(parkPath);
            auto importer = ParkImporter::CreateS6(context->GetObjectRepository());
            const auto load = importer->LoadSavedGame(parkPath.string().c_str(), false);
            context->GetObjectManager().LoadObjects(load.RequiredObjects);
            MapAnimations::ClearAll();
            auto& state = getGameState();
            importer->Import(state);
            state.entities.resetEntitySpatialIndices();
            ResetAllSpriteQuadrantPlacements();
            gDayNightCycle = 0;
            Weather::gLightningFlash = 0;
            gPaletteEffectFrame = 0;
            LoadPalette();
            EntityTweener::get().reset();
            MapAnimations::MarkAllTiles();
            FixInvalidVehicleSpriteSizes();

            ownsVideo = (SDL_WasInit(SDL_INIT_VIDEO) & SDL_INIT_VIDEO) == 0;
            if (ownsVideo && SDL_InitSubSystem(SDL_INIT_VIDEO) != 0)
                return SDL_GetError();
            window.reset(SDL_CreateWindow(
                "OpenRCT2 viewport parity", SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED, kSceneExtent.width,
                kSceneExtent.height, SDL_WINDOW_HIDDEN | Vulkan::Platform::GetRequiredSdlWindowFlags()));
            if (!window)
                return SDL_GetError();
            const auto drawable = Vulkan::Platform::GetDrawableExtent(window.get());
            if (drawable.width != kSceneExtent.width || drawable.height != kSceneExtent.height)
                return "Scene fixture requires an unscaled 640x480 drawable";
            Gpu::BackendConfig config{ .logicalExtent = kSceneExtent,
                                       .drawableExtent = kSceneExtent,
                                       .presentMode = Gpu::PresentMode::Immediate,
                                       .frameAcquireMode = Gpu::FrameAcquireMode::Wait,
                                       .shaderDirectory = shaders.string() };
            config.enableDiagnosticCapture = true;
            backend = Vulkan::CreateBackend(Vulkan::Platform::CreatePresentationHost(window.get()));
            backend->Initialise(config);
            return {};
        }

        void TearDown() override
        {
            backend.reset();
            window.reset();
            if (ownsVideo)
                SDL_QuitSubSystem(SDL_INIT_VIDEO);
            if (context != nullptr && context->GetDrawingEngine() != nullptr)
                ViewportDisposePresentation();
            context.reset();
            gOpenRCT2Headless = oldHeadless;
            gOpenRCT2NoGraphics = oldNoGraphics;
        }
    };
} // namespace

TEST_F(VulkanSceneParityTest, FrozenSmallParkViewportRotationsAndZooms)
{
    std::string unavailable;
    try
    {
        unavailable = Initialise();
    }
    catch (const std::exception& error)
    {
        unavailable = error.what();
    }
    if (!unavailable.empty())
    {
        if (RequiredSceneParity())
            FAIL() << "Required scene parity unavailable: " << unavailable;
        GTEST_SKIP() << unavailable;
    }
    const auto* ride = GetRide(RideId::FromUnderlying(0));
    ASSERT_NE(ride, nullptr);
    ASSERT_FALSE(ride->overallView.isNull());
    json_t assetMetadata = json_t::object();
    ValidateLoadedAssetMetadata(*ride, assetMetadata);
    const CoordsXYZ focus{ ride->overallView.x + 16, ride->overallView.y + 16, TileElementHeight(ride->overallView) + 32 };
    const auto palette = ScenePalette();
    backend->SetPalette(palette);
    backend->SetRemapPalette(SceneRemapTables());
    const auto* blend = GetBlendColourMap();
    ASSERT_NE(blend, nullptr);
    backend->SetBlendPalette(std::as_bytes(std::span<const BlendColourMapType>{ blend, 1 }));

    std::filesystem::path artifacts;
    if (const auto* path = std::getenv("OPENRCT2_VULKAN_PARITY_ARTIFACTS"))
        artifacts = path;
    const auto frozenTick = getGameState().currentTicks;
    const auto frozenMapRevision = MapRevisionFingerprint();
    const auto frozenPaletteFrame = gPaletteEffectFrame;
    Gpu::TextureCache textures;
    for (const bool transparentBackground : { false, true })
    {
        for (int8_t zoom = 0; zoom <= 1; zoom++)
        {
            for (uint8_t rotation = 0; rotation < 4; rotation++)
            {
                const auto fixture = std::string(transparentBackground ? "SmallParkTransparent_R" : "SmallPark_R")
                    + std::to_string(rotation) + "_Z" + std::to_string(zoom);
                SCOPED_TRACE(fixture);
                const uint64_t frameNumber = 1000 + (transparentBackground ? 100 : 0) + zoom * 4 + rotation;
                Viewport viewport{ .width = kSceneExtent.width,
                                   .height = kSceneExtent.height,
                                   .flags = VIEWPORT_FLAG_INDEPENDENT_ROTATION
                                       | (transparentBackground ? VIEWPORT_FLAG_TRANSPARENT_BACKGROUND : 0),
                                   .zoom = ZoomLevel{ zoom },
                                   .rotation = rotation };
                const auto viewPos = centre2dCoordinates(focus, &viewport);
                ASSERT_TRUE(viewPos.has_value());
                viewport.viewPos = *viewPos;
                std::vector<PaletteIndex> reference(kScenePixels);
                std::vector<PaletteIndex> addressSpace(kScenePixels);
                RenderTarget softwareTarget{ .bits = reference.data(),
                                             .width = kSceneExtent.width,
                                             .height = kSceneExtent.height };
                RenderTarget gpuTarget{ .bits = addressSpace.data(),
                                        .width = kSceneExtent.width,
                                        .height = kSceneExtent.height };
                X8DrawingContext software(nullptr);
                AuxiliaryEngine softwareEngine(software, softwareTarget);
                Gpu::CommandDrawingContext recorder(gpuTarget, textures);
                AuxiliaryEngine gpuEngine(recorder, gpuTarget);
                ASSERT_NE(&softwareEngine, context->GetDrawingEngine());
                ASSERT_NE(&gpuEngine, context->GetDrawingEngine());

                software.BeginDraw();
                software.Clear(softwareTarget, PaletteIndex::transparent);
                ViewportRender(softwareTarget, &viewport);
                software.EndDraw();
                ASSERT_GT(
                    std::count_if(
                        reference.begin(), reference.end(),
                        [](PaletteIndex pixel) { return pixel != PaletteIndex::transparent; }),
                    1000)
                    << "Reference scene did not produce a meaningful viewport";
                Gpu::FrameCommandStream commands;
                textures.BeginFrame();
                recorder.Begin(commands);
                recorder.Clear(gpuTarget, PaletteIndex::transparent);
                ViewportRender(gpuTarget, &viewport);
                recorder.End();
                ASSERT_GT(commands.opaqueSprites.size(), 0u);
                const auto lease = textures.SealFrame(commands);
                const auto frame = backend->BeginFrame(frameNumber);
                ASSERT_TRUE(frame.has_value());
                ASSERT_TRUE(backend->RequestFrameCapture(*frame));
                backend->Submit(*frame, commands);
                backend->Present(*frame);
                backend->WaitIdle();
                textures.RetireFrame(lease, Gpu::FrameRetirement::Presented);
                textures.DrainFrameRetirements();

                // Repaint the reference after GPU recording to detect draw-dependent state/cache changes.
                const auto frozenReference = reference;
                software.BeginDraw();
                software.Clear(softwareTarget, PaletteIndex::transparent);
                ViewportRender(softwareTarget, &viewport);
                software.EndDraw();
                ASSERT_EQ(reference, frozenReference) << "Paint changed the frozen scene between captures";
                ASSERT_EQ(getGameState().currentTicks, frozenTick);
                ASSERT_EQ(MapRevisionFingerprint(), frozenMapRevision);
                ASSERT_EQ(gPaletteEffectFrame, frozenPaletteFrame);

                json_t metadata{
                    { "frameNumber", frameNumber },
                    { "fixtureVersion", 2 },
                    { "clearIndex", 0 },
                    { "transparentBackground", transparentBackground },
                    { "assetMetadataValidation", assetMetadata },
                    { "softwareReference", "frozen X8 source; auxiliary ViewportRender" },
                    { "rgbaReference", "opaque SDR expansion of software indexed canvas with frozen game palette" },
                    { "coverage", "live auxiliary scene paint/ordering; excludes main presentation, UI, weather and LightFX" },
                    { "fixtureState", "imported park, no simulation updates, repeated software render verified identical" },
                    { "simulationTick", frozenTick },
                    { "mapRevisionFingerprint", frozenMapRevision },
                    { "paletteEffectFrame", frozenPaletteFrame },
                    { "rotation", rotation },
                    { "zoom", zoom },
                    { "viewPosition", { viewport.viewPos.x, viewport.viewPos.y } },
                    { "focus", { focus.x, focus.y, focus.z } },
                    { "flags", viewport.flags },
                    { "lighting", "disabled" },
                    { "scale", "1:1 nearest" },
                    { "inputHashAlgorithm", "FNV-1a-64" },
                    { "inputHashes", inputHashes },
                    { "opaqueSpriteCommands", commands.opaqueSprites.size() },
                    { "transparentRectCommands", commands.transparentRects.size() },
                    { "rct1CsgLoaded", IsCsgLoaded() },
                    { "parkPath", parkPath.string() },
                    { "shaderDirectory", shaders.string() },
                    { "objectDirectory",
                      context->GetPlatformEnvironment().GetDirectoryPath(DirBase::openrct2, DirId::objects) },
                };
                for (const auto& entry : std::filesystem::directory_iterator(shaders))
                    if (entry.path().extension() == ".spv")
                        metadata["shaderHashes"][entry.path().filename().string()] = HashFile(entry.path());
                std::vector<std::byte> indexed(kScenePixels);
                ASSERT_TRUE(backend->ReadbackLatestIndexedCanvas(kSceneExtent, indexed));
                const auto expected = std::as_bytes(std::span(reference));
                const auto capture = backend->ReadbackFrameRgba(frameNumber);
                ASSERT_TRUE(capture.has_value());
                ASSERT_EQ(capture->frameNumber, frameNumber);
                ASSERT_EQ(capture->logicalExtent, kSceneExtent);
                ASSERT_EQ(capture->drawableExtent, kSceneExtent);
                ASSERT_EQ(capture->rgba.size(), kScenePixels * 4);
                metadata["deviceName"] = capture->deviceName;
                metadata["vendorId"] = capture->vendorId;
                metadata["deviceId"] = capture->deviceId;
                metadata["driverVersion"] = capture->driverVersion;
                metadata["sourceFormat"] = capture->sourceFormat;
                metadata["paletteVersion"] = capture->paletteVersion;
                metadata["swapchainGeneration"] = capture->swapchainGeneration;
                EXPECT_EQ(
                    CompareAndReport(artifacts, fixture, "indexed", expected, indexed, 1, palette, metadata, kSceneExtent), 0u);
                EXPECT_EQ(
                    CompareAndReport(
                        artifacts, fixture, "rgba", Expand(expected, palette), capture->rgba, 4, palette, metadata,
                        kSceneExtent),
                    0u);
            }
        }
    }
}

TEST_F(VulkanSceneParityTest, MainPublicationMustMatchLiveAfterSynchronousEntityMove)
{
    const auto unavailable = Initialise(true);
    if (!unavailable.empty())
    {
        if (RequiredSceneParity())
            FAIL() << unavailable;
        GTEST_SKIP() << unavailable;
    }
    struct RestorePublicationInputs
    {
        uint32_t draw = gCurrentDrawCount;
        MapSelectFlags flags = gMapSelectFlags;
        ~RestorePublicationInputs()
        {
            gCurrentDrawCount = draw;
            gMapSelectFlags = flags;
        }
    } restoreInputs;
    context->InitialiseDrawingEngine();
    auto* mainEngine = context->GetDrawingEngine();
    ASSERT_NE(mainEngine, nullptr);
    ASSERT_NE(dynamic_cast<X8DrawingEngine*>(mainEngine), nullptr);
    mainEngine->Resize(kSceneExtent.width, kSceneExtent.height);
    auto& mainTarget = *mainEngine->getRT();
    ASSERT_EQ(mainTarget.DrawingEngine, mainEngine);
    ViewportDisposePresentation();
    gMapSelectFlags = {};
    auto& entities = getGameState().entities;
    auto* balloon = entities.createEntity<Balloon>();
    ASSERT_NE(balloon, nullptr);
    balloon->spriteData.width = 13;
    balloon->spriteData.heightMin = 22;
    balloon->spriteData.heightMax = 11;
    balloon->colour = Colour::brightRed;
    balloon->frame = 0;
    balloon->popped = 0;
    const CoordsXYZ before{ 336, 112, 240 };
    const CoordsXYZ after{ 400, 176, 240 };
    balloon->moveTo(before);
    entities.updateEntitiesSpatialIndex();
    EntityTweener::get().reset();
    Viewport viewport{ .width = kSceneExtent.width,
                       .height = kSceneExtent.height,
                       .flags = VIEWPORT_FLAG_INDEPENDENT_ROTATION,
                       .zoom = ZoomLevel{ 0 },
                       .rotation = 0 };
    const auto viewPos = centre2dCoordinates(CoordsXYZ{ 336, 112, 144 }, &viewport);
    ASSERT_TRUE(viewPos.has_value());
    viewport.viewPos = *viewPos;
    auto drawMain = [&]() {
        mainEngine->BeginDraw();
        auto* drawing = mainEngine->GetDrawingContext();
        drawing->Clear(mainTarget, PaletteIndex::transparent);
        ViewportRender(mainTarget, &viewport);
        mainEngine->EndDraw();
        std::vector<PaletteIndex> pixels(kScenePixels);
        for (uint32_t y = 0; y < kSceneExtent.height; y++)
            std::copy_n(
                mainTarget.bits + y * mainTarget.LineStride(), kSceneExtent.width, pixels.begin() + y * kSceneExtent.width);
        return pixels;
    };
    gCurrentDrawCount++;
    const auto initial = drawMain();
    // Complete the old-state entity preparation. The following synchronous frame must still use the current live state.
    context->GetJobPool().Join();
    balloon->moveTo(after);
    entities.updateEntitiesSpatialIndex();
    EntityTweener::get().reset();
    // Nonempty transient-state flags demand synchronous publication without drawing selection tiles.
    gMapSelectFlags.set(MapSelectFlag::green);
    gCurrentDrawCount++;
    const auto published = drawMain();
    std::vector<PaletteIndex> live(kScenePixels);
    RenderTarget liveTarget{ .bits = live.data(), .width = kSceneExtent.width, .height = kSceneExtent.height };
    X8DrawingContext liveDrawing(nullptr);
    AuxiliaryEngine liveEngine(liveDrawing, liveTarget);
    liveDrawing.BeginDraw();
    liveDrawing.Clear(liveTarget, PaletteIndex::transparent);
    ViewportRender(liveTarget, &viewport);
    liveDrawing.EndDraw();
    ASSERT_NE(live, initial) << "Controlled balloon movement must change visible pixels";
    const auto palette = ScenePalette();
    std::filesystem::path artifacts;
    if (const auto* path = std::getenv("OPENRCT2_VULKAN_PARITY_ARTIFACTS"))
        artifacts = path;
    const json_t metadata{
        { "fixtureVersion", 1 },
        { "frameNumber", 2000 },
        { "referenceImageLabel", "live" },
        { "resultImageLabel", "published" },
        { "reference", "frozen X8 rasterizer; auxiliary live ViewportRender" },
        { "result", "same frozen X8 rasterizer; context main PresentationScene ViewportRender" },
        { "coverage", "main publication versus live source; no Vulkan rasterization or UI presentation claim" },
        { "entityBefore", { before.x, before.y, before.z } },
        { "entityAfter", { after.x, after.y, after.z } },
        { "simulationTick", getGameState().currentTicks },
        { "inputHashes", inputHashes },
        { "rotation", 0 },
        { "zoom", 0 },
        { "viewPosition", { viewport.viewPos.x, viewport.viewPos.y } },
        { "synchronousPublication", true },
    };
    const auto expected = std::as_bytes(std::span(live));
    const auto actual = std::as_bytes(std::span(published));
    EXPECT_EQ(
        CompareAndReport(
            artifacts, "MainPublication_EntityMove", "indexed", expected, actual, 1, palette, metadata, kSceneExtent),
        0u);
    EXPECT_EQ(
        CompareAndReport(
            artifacts, "MainPublication_EntityMove", "rgba", Expand(expected, palette), Expand(actual, palette), 4, palette,
            metadata, kSceneExtent),
        0u);
}

#else

TEST(VulkanSceneParityTest, RequiredBackendAvailability)
{
    if (RequiredSceneParity())
        FAIL() << "OPENRCT2_REQUIRE_VULKAN_TESTS=1 requires an ENABLE_VULKAN build";
    GTEST_SKIP() << "Vulkan was not compiled";
}

#endif
