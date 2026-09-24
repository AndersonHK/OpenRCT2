/*****************************************************************************
 * Copyright (c) 2014-2026 OpenRCT2 developers
 *
 * For a complete list of all authors, please refer to contributors.md
 * Interested in contributing? Visit https://github.com/OpenRCT2/OpenRCT2
 *
 * OpenRCT2 is licensed under the GNU General Public License version 3.
 *****************************************************************************/

#include "ParkPreview.h"

#include "../Context.h"
#include "../Diagnostic.h"
#include "../GameState.h"
#include "../OpenRCT2.h"
#include "../SpriteIds.h"
#include "../drawing/Drawing.Sprite.h"
#include "../drawing/Drawing.h"
#include "../drawing/NewDrawing.h"
#include "../drawing/Palette.h"
#include "../drawing/RenderService.h"
#include "../drawing/RenderTarget.h"
#include "../interface/Viewport.h"
#include "../interface/WindowTypes.h"
#include "../object/TerrainSurfaceObject.h"
#include "../ride/RideManager.hpp"
#include "../world/Map.h"
#include "../world/tile_element/SurfaceElement.h"
#include "../world/tile_element/TileElement.h"

#include <cstring>
#include <optional>

namespace OpenRCT2
{
    static std::optional<PreviewImage> generatePreviewMap();
    static std::optional<PreviewImage> generatePreviewScreenshot();

    using OpenRCT2::Drawing::PaletteIndex;

    ParkPreview generatePreviewFromGameState(const GameState_t& gameState)
    {
        ParkPreview preview{
            .parkName = gameState.park.name,
            .parkRating = gameState.park.rating,
            .year = gameState.date.GetYear(),
            .month = gameState.date.GetMonth(),
            .day = gameState.date.GetDay(),
            .parkUsesMoney = !(gameState.park.flags.has(ParkFlag::noMoney)),
            .cash = gameState.park.cash,
            .numRides = static_cast<uint16_t>(RideManager(gameState).size()),
            .numGuests = static_cast<uint16_t>(gameState.park.numGuestsInPark),
        };

        if (auto image = generatePreviewMap(); image != std::nullopt)
            preview.images.push_back(*image);

        try
        {
            if (auto image = generatePreviewScreenshot(); image != std::nullopt)
                preview.images.push_back(*image);
        }
        catch (const std::exception& error)
        {
            // The screenshot is optional save metadata. A device failure must not prevent the park from being saved.
            LOG_ERROR("Park screenshot preview omitted: %s", error.what());
        }

        return preview;
    }

    static uint8_t _tileColourIndex = 0;

    static PaletteIndex getPreviewColourByTilePos(const TileCoordsXY& pos)
    {
        PaletteIndex paletteIndex = PaletteIndex::transparent;

        auto tileElement = MapGetFirstElementAt(pos);
        if (tileElement == nullptr)
            return paletteIndex;

        PaletteIndex surfaceColour = paletteIndex;
        bool isOutsidePark = false;
        do
        {
            switch (tileElement->getType())
            {
                case TileElementType::surface:
                {
                    auto* surfaceElement = tileElement->asSurface();
                    if (surfaceElement == nullptr)
                    {
                        surfaceColour = paletteIndex = PaletteIndex::transparent;
                        break;
                    }

                    if (surfaceElement->getWaterHeight() > 0)
                    {
                        surfaceColour = paletteIndex = PaletteIndex::pi195;
                    }
                    else
                    {
                        const auto* surfaceObject = surfaceElement->getSurfaceObject();
                        if (surfaceObject != nullptr)
                        {
                            surfaceColour = paletteIndex = surfaceObject->MapColours[_tileColourIndex];
                        }
                    }

                    isOutsidePark |= !(surfaceElement->hasOwnership(OwnershipFlag::landOwned));
                    break;
                }

                case TileElementType::path:
                    paletteIndex = PaletteIndex::pi17;
                    break;

                case TileElementType::track:
                    paletteIndex = PaletteIndex::pi183;
                    break;

                case TileElementType::smallScenery:
                case TileElementType::largeScenery:
                    paletteIndex = PaletteIndex::pi99;
                    break;

                case TileElementType::entrance:
                    paletteIndex = PaletteIndex::pi186;
                    break;

                default:
                    break;
            }
        } while (!(tileElement++)->isLastForTile());

        // Darken every other tile that's outside of the park, unless it's a path
        if (isOutsidePark && _tileColourIndex == 1 && paletteIndex != PaletteIndex::pi17)
            paletteIndex = PaletteIndex::pi10;
        // For rides, every other tile should use the surface colour
        else if (_tileColourIndex == 1 && paletteIndex == PaletteIndex::pi183)
            paletteIndex = surfaceColour;

        _tileColourIndex = (_tileColourIndex + 1) % 2;

        return paletteIndex;
    }

    // 0x0046DB4C
    static std::optional<PreviewImage> generatePreviewMap()
    {
        const auto& gameState = getGameState();
        const auto drawableMapSize = TileCoordsXY{ gameState.mapSize.x - 2, gameState.mapSize.y - 2 };
        const auto longEdgeSize = std::max(drawableMapSize.x, drawableMapSize.y);
        const auto idealPreviewSize = 150;

        auto longEdgeSizeLeft = longEdgeSize;
        uint8_t mapSkipFactor = 1;
        while (longEdgeSizeLeft > idealPreviewSize)
        {
            longEdgeSizeLeft -= idealPreviewSize;
            mapSkipFactor++;
        }

        const uint8_t previewWidth = std::max(1, drawableMapSize.x / mapSkipFactor);
        const uint8_t previewHeight = std::max(1, drawableMapSize.y / mapSkipFactor);

        PreviewImage image{
            .type = PreviewImageType::miniMap,
            .width = previewWidth,
            .height = previewHeight,
        };

        for (auto y = 0u; y < image.height; y++)
        {
            int32_t mapY = 1 + (y * mapSkipFactor);
            if (mapY > drawableMapSize.y)
                break;

            _tileColourIndex = y % 2;

            for (auto x = 0u; x < image.width; x++)
            {
                int32_t mapX = drawableMapSize.x - (x * mapSkipFactor);
                if (mapX < 1)
                    break;

                image.pixels[y * image.width + x] = getPreviewColourByTilePos({ mapX, mapY });
            }
        }

        return image;
    }

    static std::optional<PreviewImage> generatePreviewScreenshot()
    {
        if (gOpenRCT2NoGraphics)
            return std::nullopt;

        const auto& gameState = getGameState();
        const auto mainWindow = WindowGetMain();
        const auto mainViewport = WindowGetViewport(mainWindow);

        CoordsXYZD mapPosXYZD{};
        if (mainViewport != nullptr)
        {
            const auto centre = mainViewport->viewPos
                + ScreenCoordsXY{ mainViewport->ViewWidth() / 2, mainViewport->ViewHeight() / 2 };
            const auto mapPos = ViewportPosToMapPos(centre, 24, mainViewport->rotation);
            mapPosXYZD = CoordsXYZD(mapPos.x, mapPos.y, int32_t{ TileElementHeight(mapPos) }, mainViewport->rotation);
        }
        else if (!gameState.park.entrances.empty())
        {
            const auto& entrance = gameState.park.entrances[0];
            mapPosXYZD = CoordsXYZD(entrance.x + 16, entrance.y + 16, entrance.z + 32, DirectionReverse(entrance.direction));
        }
        else
        {
            return std::nullopt;
        }

        PreviewImage image{
            .type = PreviewImageType::screenshot,
            .width = kMaxPreviewImageSize,
            .height = kMaxPreviewImageSize / 5 * 4, // 5:4 ratio
        };

        Viewport saveVp{
            .width = image.width,
            .height = image.height,
            .flags = 0,
            .zoom = ZoomLevel{ 1 },
            .rotation = mapPosXYZD.direction,
        };

        auto viewPos = centre2dCoordinates(mapPosXYZD, &saveVp);
        if (viewPos == std::nullopt)
            return std::nullopt;

        saveVp.viewPos = *viewPos;

        Drawing::OffscreenRenderRequest request;
        request.name = "park-preview";
        request.logicalExtent = { image.width, image.height };
        request.outputExtent = request.logicalExtent;
        for (size_t i = 0; i < request.palette.size(); ++i)
        {
            const auto colour = Drawing::gPalette[i];
            request.palette[i] = { colour.red, colour.green, colour.blue, colour.alpha };
        }
        const auto expectedPalette = request.palette;
        auto session = GetContext()->GetRenderService().BeginOffscreen(std::move(request));
        if (!session)
            throw Drawing::RenderServiceException(
                { Drawing::RenderErrorCode::creationFailed, "Park preview has no render session" });
        auto& rt = session->GetRenderTarget();
        if (rt.x != 0 || rt.y != 0 || rt.width != image.width || rt.height != image.height
            || rt.bits == nullptr || rt.DrawingEngine == nullptr || rt.pitch < 0)
        {
            throw Drawing::RenderServiceException(
                { Drawing::RenderErrorCode::executionFailed, "Park preview render target differs" });
        }
        rt.zoom_level = saveVp.zoom;
        ViewportRender(rt, &saveVp);
        auto completion = session->Submit();
        if (!completion)
            throw Drawing::RenderServiceException(
                { Drawing::RenderErrorCode::executionFailed, "Park preview has no render completion" });
        const auto outcome = completion->Wait(std::chrono::seconds(120));
        if (outcome.error)
            throw Drawing::RenderServiceException(*outcome.error);
        if (!outcome.result)
            throw Drawing::RenderServiceException(
                { Drawing::RenderErrorCode::executionFailed, "Park preview has no owned output" });
        const auto& result = *outcome.result;
        const Drawing::RenderExtent expectedExtent{ image.width, image.height };
        const auto expectedBytes = static_cast<size_t>(image.width) * image.height;
        if (outcome.identity != completion->GetIdentity() || result.identity != outcome.identity
            || result.identity.submissionId == 0 || result.identity.targetId == 0 || result.identity.targetGeneration == 0
            || result.identity.name != "park-preview" || result.logicalExtent != expectedExtent
            || result.outputExtent != expectedExtent || result.palette != expectedPalette
            || result.indexed.size() != expectedBytes || !result.rgba.empty())
        {
            throw Drawing::RenderServiceException(
                { Drawing::RenderErrorCode::executionFailed, "Park preview readback contract differs" });
        }
        std::memcpy(image.pixels, result.indexed.data(), expectedBytes);

        return image;
    }

    void drawPreviewImage(const PreviewImage& image, Drawing::RenderTarget& rt, ScreenCoordsXY screenPos)
    {
        G1Element g1temp = {};
        g1temp.offset = reinterpret_cast<uint8_t*>(const_cast<PaletteIndex*>(image.pixels));
        g1temp.width = image.width;
        g1temp.height = image.height;

        GfxSetG1Element(SPR_TEMP_PARK_PREVIEW, &g1temp);
        DrawingEngineInvalidateImage(SPR_TEMP_PARK_PREVIEW);
        GfxDrawSprite(rt, ImageId(SPR_TEMP_PARK_PREVIEW), screenPos);
    }

} // namespace OpenRCT2
