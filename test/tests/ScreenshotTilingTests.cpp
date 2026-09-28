/*****************************************************************************
 * Copyright (c) 2014-2026 OpenRCT2 developers. GPL-3.0-or-later.
 *****************************************************************************/
#include <gtest/gtest.h>
#include <openrct2/interface/ScreenshotTiling.h>
#include <openrct2/drawing/RenderTarget.h>
#include <cstring>
#include <limits>
#include <stdexcept>

using namespace OpenRCT2;
using namespace OpenRCT2::Drawing;
namespace Tiling = OpenRCT2::ScreenshotTiling;
namespace
{
    enum class Fault { none, failure, extent, palette, shortIndices, rgba, identity };
    struct State
    {
        size_t begun{}, waited{}, live{};
        size_t failAt = 1;
        Fault fault{};
        std::vector<RenderExtent> extents;
    };
    class Completion final : public IRenderCompletion
    {
        State& _state;
        RenderOutcome _outcome;
    public:
        Completion(State& state, RenderOutcome outcome) : _state(state), _outcome(std::move(outcome)) {}
        const RenderSubmissionIdentity& GetIdentity() const noexcept override { return _outcome.identity; }
        RenderOutcome Wait(std::chrono::milliseconds timeout) override
        {
            EXPECT_GT(timeout.count(), 0);
            ++_state.waited;
            return _outcome;
        }
        void Cancel() override {}
    };
    class Session final : public IRenderSession
    {
        State& _state;
        OffscreenRenderRequest _request;
        std::vector<std::byte> _pixels;
        RenderTarget _target;
        bool _submitted{};
    public:
        Session(State& state, OffscreenRenderRequest request) : _state(state), _request(std::move(request))
        {
            _pixels.resize(static_cast<size_t>(_request.logicalExtent.width) * _request.logicalExtent.height);
            _target.bits = reinterpret_cast<PaletteIndex*>(_pixels.data());
            _target.width = static_cast<int32_t>(_request.logicalExtent.width);
            _target.height = static_cast<int32_t>(_request.logicalExtent.height);
            ++_state.live;
        }
        ~Session() override { --_state.live; }
        IDrawingContext& GetDrawingContext() override { throw std::logic_error("CPU test does not draw through a renderer"); }
        RenderTarget& GetRenderTarget() override { return _target; }
        std::shared_ptr<IRenderCompletion> Submit() override
        {
            EXPECT_FALSE(_submitted);
            _submitted = true;
            RenderResult result;
            result.identity = { _state.begun, 1, _state.begun, _request.name };
            result.logicalExtent = _request.logicalExtent;
            result.outputExtent = _request.outputExtent;
            result.palette = _request.palette;
            result.indexed = _pixels;
            RenderOutcome outcome{ result.identity, nullptr, {} };
            const auto fault = _state.begun == _state.failAt ? _state.fault : Fault::none;
            switch (fault)
            {
                case Fault::failure:
                    outcome.error = RenderError{ RenderErrorCode::deviceLost, "Synthetic second tile failed" };
                    break;
                case Fault::extent: result.logicalExtent.width++; break;
                case Fault::palette: result.palette[37].red ^= 1; break;
                case Fault::shortIndices: result.indexed.pop_back(); break;
                case Fault::rgba: result.rgba.push_back(std::byte{ 0 }); break;
                case Fault::identity: result.identity.targetGeneration++; break;
                case Fault::none: break;
            }
            if (!outcome.error)
                outcome.result = std::make_shared<const RenderResult>(std::move(result));
            return std::make_shared<Completion>(_state, std::move(outcome));
        }
        void Cancel() noexcept override {}
    };
    class Service final : public IRenderService
    {
    public:
        State state;
        std::unique_ptr<IRenderSession> BeginOffscreen(OffscreenRenderRequest request) override
        {
            EXPECT_EQ(state.live, 0u);
            EXPECT_EQ(state.begun, state.waited);
            EXPECT_TRUE(request.indexedOutput);
            EXPECT_FALSE(request.rgbaOutput);
            EXPECT_EQ(request.alphaPolicy, RenderAlphaPolicy::transparentIndexZero);
            EXPECT_EQ(request.clearIndex, 0u);
            EXPECT_LE(request.logicalExtent.width, 2048u);
            EXPECT_LE(request.logicalExtent.height, 2048u);
            EXPECT_LE(static_cast<uint64_t>(request.logicalExtent.width) * request.logicalExtent.height, 4194304u);
            state.extents.push_back(request.logicalExtent);
            ++state.begun;
            return std::make_unique<Session>(state, std::move(request));
        }
        void Shutdown() noexcept override {}
    };
    Viewport View(int32_t width, int32_t height)
    {
        Viewport viewport{};
        viewport.width = width;
        viewport.height = height;
        viewport.viewPos = { -959, -557 };
        return viewport;
    }
    uint8_t Pattern(uint32_t x, uint32_t y)
    {
        return static_cast<uint8_t>((x * 13 + y * 19) % 251 + 1);
    }
    void PaintPattern(RenderTarget& target, const Viewport& viewport)
    {
        // Synthetic tile content keyed by final image coordinates; no software renderer is invoked.
        for (int32_t y = 0; y < target.height; ++y)
            for (int32_t x = 0; x < target.width; ++x)
                target.bits[y * target.LineStride() + x] = static_cast<PaletteIndex>(
                    Pattern(static_cast<uint32_t>(x - viewport.pos.x), static_cast<uint32_t>(y - viewport.pos.y)));
    }
    int64_t FloorDivide(int64_t value, int64_t divisor)
    {
        const auto q = value / divisor;
        return q - (value < 0 && value % divisor != 0 ? 1 : 0);
    }
}

TEST(ScreenshotTilingTest, PreservesFullRasterPhaseAndColumnsAtOddOriginsAndTileSeams)
{
    for (int8_t zoom = -2; zoom <= 3; ++zoom)
        for (uint8_t rotation = 0; rotation < 4; ++rotation)
            for (const auto origin : { ScreenCoordsXY{ -959, -557 }, ScreenCoordsXY{ 17, -1 }, ScreenCoordsXY{ -1, 33 } })
                for (const int32_t tileX : { 0, 2047, 2048, 4096 })
                    for (const int32_t tileY : { 0, 2047, 2048, 4096 })
                    {
                        auto whole = View(6001, 5003);
                        whole.viewPos = origin;
                        whole.zoom = ZoomLevel{ zoom };
                        whole.rotation = rotation;
                        whole.flags = VIEWPORT_FLAG_TRANSPARENT_BACKGROUND | VIEWPORT_FLAG_GRIDLINES;
                        const auto tile = Tiling::TileViewport(whole, { tileX, tileY, 513, 517 });
                        EXPECT_EQ(tile.viewPos, origin);
                        EXPECT_EQ(tile.width, whole.width);
                        EXPECT_EQ(tile.height, whole.height);
                        EXPECT_EQ(tile.rotation, rotation);
                        EXPECT_EQ(tile.zoom, whole.zoom);
                        EXPECT_EQ(tile.flags, whole.flags);
                        const auto scale = int64_t{ 1 } << (zoom < 0 ? -zoom : zoom);
                        const auto referenceX = zoom < 0 ? static_cast<int64_t>(origin.x) * scale : FloorDivide(origin.x, scale);
                        const auto referenceY = zoom < 0 ? static_cast<int64_t>(origin.y) * scale : FloorDivide(origin.y, scale);
                        for (const int32_t local : { 0, 1, 511, 512 })
                        {
                            const auto x = tile.zoom.ApplyInversedTo(tile.viewPos.x) - tile.pos.x + local;
                            const auto y = tile.zoom.ApplyInversedTo(tile.viewPos.y) - tile.pos.y + local;
                            EXPECT_EQ(x, referenceX + tileX + local);
                            EXPECT_EQ(y, referenceY + tileY + local);
                            const auto columnWidth = tile.zoom.ApplyInversedTo(32);
                            EXPECT_EQ(FloorDivide(x, columnWidth), FloorDivide(referenceX + tileX + local, columnWidth));
                        }
                    }
}

TEST(ScreenshotTilingTest, SequentialBoundedTilesAssembleExactOwnedIndexedImage)
{
    Service service;
    GamePalette palette{};
    palette[37] = { 7, 19, 43, 91 };
    const auto image = Tiling::Render(service, View(2049, 2051), palette, PaintPattern);
    EXPECT_EQ(service.state.begun, 4u);
    EXPECT_EQ(service.state.waited, 4u);
    EXPECT_EQ(service.state.live, 0u);
    const std::vector<RenderExtent> expected{ { 2048, 2048 }, { 1, 2048 }, { 2048, 3 }, { 1, 3 } };
    EXPECT_EQ(service.state.extents, expected);
    ASSERT_EQ(image.Pixels.size(), 2049u * 2051u);
    EXPECT_EQ(image.Width, 2049u);
    EXPECT_EQ(image.Height, 2051u);
    EXPECT_EQ(image.Stride, 2049u);
    EXPECT_EQ(image.Depth, 8u);
    ASSERT_TRUE(image.Palette.has_value());
    EXPECT_EQ((*image.Palette)[37].blue, 7u);
    EXPECT_EQ((*image.Palette)[37].green, 19u);
    EXPECT_EQ((*image.Palette)[37].red, 43u);
    EXPECT_EQ((*image.Palette)[37].alpha, 91u);
    for (uint32_t y = 0; y < image.Height; ++y)
        for (uint32_t x = 0; x < image.Width; ++x)
            if (image.Pixels[static_cast<size_t>(y) * image.Stride + x] != Pattern(x, y))
                FAIL() << "Composition mismatch at " << x << "," << y;
}

TEST(ScreenshotTilingTest, TileFailureStopsBeforeLaterTilesAndReturnsNoPartialImage)
{
    Service service;
    service.state.fault = Fault::failure;
    service.state.failAt = 2;
    Image output;
    output.Pixels = { 99 };
    try
    {
        output = Tiling::Render(service, View(4097, 1), {}, PaintPattern);
        FAIL() << "Expected second tile error";
    }
    catch (const RenderServiceException& error) { EXPECT_EQ(error.GetCode(), RenderErrorCode::deviceLost); }
    EXPECT_EQ(output.Pixels, std::vector<uint8_t>{ 99 });
    EXPECT_EQ(service.state.begun, 2u);
    EXPECT_EQ(service.state.waited, 2u);
    EXPECT_EQ(service.state.live, 0u);
    Service recordingFailure;
    EXPECT_THROW(Tiling::Render(recordingFailure, View(4097, 1), {},
        [](RenderTarget&, const Viewport&) { throw std::runtime_error("Synthetic recording failure"); }), std::runtime_error);
    EXPECT_EQ(recordingFailure.state.begun, 1u);
    EXPECT_EQ(recordingFailure.state.waited, 0u);
    EXPECT_EQ(recordingFailure.state.live, 0u);
}

TEST(ScreenshotTilingTest, RejectsMalformedReadbacksBeforeReturningAnImage)
{
    for (const auto fault : { Fault::extent, Fault::palette, Fault::shortIndices, Fault::rgba, Fault::identity })
    {
        Service service;
        service.state.fault = fault;
        EXPECT_THROW(Tiling::Render(service, View(3, 5), {}, PaintPattern), RenderServiceException);
        EXPECT_EQ(service.state.begun, 1u);
        EXPECT_EQ(service.state.live, 0u);
    }
}

TEST(ScreenshotTilingTest, InvalidGeometryRejectsBeforeCreatingAuxiliaryResources)
{
    Service service;
    EXPECT_THROW(Tiling::Render(service, View(0, 1), {}, PaintPattern), RenderServiceException);
    EXPECT_THROW(Tiling::Render(service, View(1, -1), {}, PaintPattern), RenderServiceException);
    auto viewport = View(10, 20);
    viewport.zoom = ZoomLevel{ 4 };
    EXPECT_THROW(Tiling::Render(service, viewport, {}, PaintPattern), RenderServiceException);
    viewport.zoom = ZoomLevel{};
    viewport.pos.x = 1;
    EXPECT_THROW(Tiling::Render(service, viewport, {}, PaintPattern), RenderServiceException);
    EXPECT_THROW(Tiling::Render(service, View(1, 1), {}, {}), RenderServiceException);
    EXPECT_THROW(Tiling::TileViewport(View(4097, 4097), { -1, 0, 1, 1 }), RenderServiceException);
    EXPECT_THROW(Tiling::TileViewport(View(4097, 4097), { 0, 0, 2049, 1 }), RenderServiceException);
    EXPECT_THROW(Tiling::TileViewport(View(4097, 4097), { 4096, 0, 2, 1 }), RenderServiceException);
    EXPECT_THROW(Tiling::TileViewport(View(std::numeric_limits<int32_t>::max(), 1),
        { std::numeric_limits<int32_t>::max(), 0, 1, 1 }), RenderServiceException);
    EXPECT_EQ(service.state.begun, 0u);
}
