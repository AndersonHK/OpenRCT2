/*****************************************************************************
 * Copyright (c) 2014-2026 OpenRCT2 developers
 *
 * For a complete list of all authors, please refer to contributors.md
 * Interested in contributing? Visit https://github.com/OpenRCT2/OpenRCT2
 *
 * OpenRCT2 is licensed under the GNU General Public License version 3.
 *****************************************************************************/

#pragma once

#include "GpuCommandStream.h"
#include "GpuTextureCache.h"
#include "PeepAssetGeneration.h"
#include "TerrainPresentationBridge.h"

#include <array>
#include <cstddef>
#include <openrct2/drawing/IDrawingContext.h>
#include <openrct2/interface/ScreenCoords.hpp>
#include <unordered_map>
#include <vector>

namespace OpenRCT2::Drawing
{
    struct RenderTarget;
}

namespace OpenRCT2::Ui::Gpu
{
    /** Records the legacy indexed drawing contract without rasterising a canvas. */
    class CommandDrawingContext final : public Drawing::IDrawingContext
    {
    private:
        Drawing::RenderTarget& _mainTarget;
        TextureCache& _textureCache;
        FrameCommandStream* _commands = nullptr;
        int32_t _drawCount = 0;
        bool _inDraw = false;
        bool _admittingTerrainScene{};
        Terrain::PresentationBridge _terrainBridge;
        PeepAssetResolver _peepAssets;
        bool DrawCompleteTerrainScene(Drawing::RenderTarget&, const PresentationGeneration&, const OrthographicCamera&);

        struct ClippingCacheEntry
        {
            const Drawing::PaletteIndex* bits = nullptr;
            int32_t width = 0;
            int32_t height = 0;
            int32_t stride = 0;
            ScreenRect clip{ 0, 0, 0, 0 };
        };

        static constexpr size_t kClippingCacheSize = 16;
        mutable std::array<ClippingCacheEntry, kClippingCacheSize> _clippingCache{};

        struct PublishedSurfaceChunk
        {
            uint64_t sourceRevision{}, pathRevision{}, objectRevision{};
            std::shared_ptr<const WorldSurfaceChunk> gpu;
        };
        std::vector<PublishedSurfaceChunk> _surfaceChunks;
        std::shared_ptr<const WorldSurfaceSpriteTable> _publishedSurfaceSprites;
        std::shared_ptr<const WorldBannerTextData> _publishedBannerTexts;
        uint64_t _nextSurfaceSpriteRevision{};
        uint64_t _nextSurfaceChunkRevision{};
        uint64_t _surfaceWorldEpoch{};
        uint32_t _surfaceWidth{};
        uint32_t _surfaceHeight{};
        bool _surfaceUsesZeroCoverage{};
        bool _nativeBalloonFixture{};
        bool _nativeTerrainFixture{};
        bool _admittingBalloonFixture{};
        bool _balloonEligibility{};
        uint64_t _balloonEligibilityEpoch{}, _balloonEligibilityRevision{}, _balloonSpriteRevision{};
        std::shared_ptr<const BalloonSpriteTable> _balloonSprites;
        [[nodiscard]] bool IsBalloonFixtureEligible(const PresentationGeneration& generation);
        [[nodiscard]] std::shared_ptr<const BalloonSpriteTable> ResolveBalloonSprites();

    public:
        CommandDrawingContext(Drawing::RenderTarget& mainTarget, TextureCache& textureCache);

        void Begin(FrameCommandStream& commands);
        // Factory default is false; only the isolated diagnostic frontend opts in.
        void SetNativeBalloonFixture(bool enabled) noexcept
        {
            _nativeBalloonFixture = enabled;
        }
        // Only the isolated diagnostic frontend calls this; ordinary factories leave false.
        void SetNativeTerrainFixtureForTesting(bool enabled) noexcept
        {
            _nativeTerrainFixture = enabled;
        }
        [[nodiscard]] std::array<uint64_t, 4> GetTerrainPreparationForTesting() const noexcept
        {
            return { _terrainBridge.GetSnapshot().worldEpoch, _terrainBridge.GetMaterialMapCopies(),
                     _terrainBridge.GetSpriteCatalogBuilds(), _terrainBridge.GetResidencyRebinds() };
        }
        [[nodiscard]] static bool AreBalloonRecordsSupported(const Drawing::RetainedBalloonSnapshot& snapshot);
        Drawing::NativeWorldCategories DrawWorldScene(
            Drawing::RenderTarget& rt, std::shared_ptr<const PresentationGeneration> generation,
            const OrthographicCamera& camera) override;
        void SealWorldScene(const Drawing::NativeWorldCategories& categories) override;
        void End();
        void Resize();
        [[nodiscard]] bool IsActive() const noexcept;

        void Clear(Drawing::RenderTarget& rt, Drawing::PaletteIndex paletteIndex) override;
        void FillRect(
            Drawing::RenderTarget& rt, Drawing::PaletteIndex paletteIndex, int32_t left, int32_t top, int32_t right,
            int32_t bottom, bool crossHatch = false) override;
        void FilterRect(
            Drawing::RenderTarget& rt, Drawing::FilterPaletteID palette, int32_t left, int32_t top, int32_t right,
            int32_t bottom) override;
        void DrawLine(Drawing::RenderTarget& rt, Drawing::PaletteIndex colour, const ScreenLine& line) override;
        void DrawSprite(Drawing::RenderTarget& rt, ImageId imageId, int32_t x, int32_t y) override;
        void DrawSpriteRawMasked(
            Drawing::RenderTarget& rt, int32_t x, int32_t y, ImageId maskImage, ImageId colourImage) override;
        void DrawSpriteSolid(
            Drawing::RenderTarget& rt, ImageId image, int32_t x, int32_t y, Drawing::PaletteIndex colour) override;
        void DrawGlyph(
            Drawing::RenderTarget& rt, ImageId image, int32_t x, int32_t y, const Drawing::PaletteMap& palette) override;
        void DrawTTFBitmap(
            Drawing::RenderTarget& rt, const Drawing::TextDrawInfo& info, TTFSurface* surface, int32_t x, int32_t y,
            uint8_t hintingThreshold) override;
        bool DrawWorldSurfaceScene(
            Drawing::RenderTarget& rt, std::shared_ptr<const PresentationGeneration> generation,
            const OrthographicCamera& camera) override;

    private:
        [[nodiscard]] std::shared_ptr<const SelectedVehiclePaintPacket> ResolveSelectedVehiclePaint(
            const PresentationGeneration& generation, const OrthographicCamera& camera);
        [[nodiscard]] WorldSurfaceSpriteSet ResolveSurfaceSpriteSet(
            ImageId image, std::vector<uint64_t>* residencies = nullptr, std::vector<uint32_t>* dependencies = nullptr);
        RectCommand& AppendRect(CommandBatch<RectCommand>& batch, const ScreenRect& clip, Int4 bounds, float zoom = 1.0f);
        [[nodiscard]] ScreenRect CalculateClipping(const Drawing::RenderTarget& rt) const;
    };
} // namespace OpenRCT2::Ui::Gpu
