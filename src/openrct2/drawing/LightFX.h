/*****************************************************************************
 * Copyright (c) 2014-2026 OpenRCT2 developers
 *
 * For a complete list of all authors, please refer to contributors.md
 * Interested in contributing? Visit https://github.com/OpenRCT2/OpenRCT2
 *
 * OpenRCT2 is licensed under the GNU General Public License version 3.
 *****************************************************************************/

#pragma once

#include "ColourPalette.h"

#include <cstddef>
#include <cstdint>
#include <limits>
#include <span>
#include <vector>

struct CoordsXY;
struct Vehicle;
struct CoordsXYZ;

namespace OpenRCT2
{
    struct EntityBase;
    struct Viewport;
} // namespace OpenRCT2

namespace OpenRCT2::Drawing
{
    struct RenderTarget;
    enum class PaletteIndex : uint8_t;
} // namespace OpenRCT2::Drawing

namespace OpenRCT2::Drawing::LightFx
{
    /**
     * Immutable output of the legacy viewport/light resolver. Render backends
     * may retain this after paint traversal without reading LightFX globals or
     * live map/entity state.
     */
    struct FrameSnapshot
    {
        struct ResolvedLight
        {
            int32_t destinationX = 0;
            int32_t destinationY = 0;
            uint32_t width = 0;
            uint32_t height = 0;
            uint32_t sourceOffset = 0;
            uint32_t sourceStride = 0;
            uint32_t type = 0;
            uint32_t intensity = 0;
        };

        uint32_t width = 0;
        uint32_t height = 0;
        GamePalette lightPalette{};
        std::vector<std::byte> intensities;
        std::vector<ResolvedLight> lights;

        [[nodiscard]] bool IsValid() const noexcept
        {
            return width != 0 && height != 0 && width <= std::numeric_limits<size_t>::max() / height
                && (intensities.empty() || intensities.size() == static_cast<size_t>(width) * height);
        }
    };

    enum class LightType : uint8_t
    {
        none = 0,

        lantern0 = 4,
        lantern1 = 5,
        lantern2 = 6,
        lantern3 = 7,

        spot0 = 8,
        spot1 = 9,
        spot2 = 10,
        spot3 = 11,
    };

    [[nodiscard]] bool ResolveLightCommandForCanvas(
        int32_t centreX, int32_t centreY, uint32_t canvasWidth, uint32_t canvasHeight, LightType type,
        uint8_t intensity, FrameSnapshot::ResolvedLight& resolved) noexcept;
    [[nodiscard]] bool RasterizeResolvedLightCommands(
        uint32_t canvasWidth, uint32_t canvasHeight, std::span<const FrameSnapshot::ResolvedLight> lights,
        std::span<uint8_t> intensities) noexcept;

    void SetAvailable(bool available);
    bool IsAvailable();
    bool ForVehiclesIsAvailable();

    void Init();
    [[nodiscard]] std::vector<std::byte> CaptureBakedFalloffs();

    void UpdateBuffers(RenderTarget&);
    const GamePalette& GetPalette();
    [[nodiscard]] bool CaptureFrameSnapshot(
        const Viewport& vp, uint32_t width, uint32_t height, FrameSnapshot& snapshot, bool includeCpuIntensity);

    void Add3DLight(const EntityBase& entity, uint8_t id, const CoordsXYZ& loc, LightType lightType);

    void Add3DLightMagicFromDrawingTile(
        const CoordsXY& mapPosition, int16_t offsetX, int16_t offsetY, int16_t offsetZ, LightType lightType);

    void AddLightsMagicVehicle(const Vehicle* vehicle);
    void AddLightsMagicVehicle_ObservationTower(const Vehicle* vehicle);
    void AddLightsMagicVehicle_MineTrainCoaster(const Vehicle* vehicle);
    void AddLightsMagicVehicle_ChairLift(const Vehicle* vehicle);
    void AddLightsMagicVehicle_BoatHire(const Vehicle* vehicle);
    void AddLightsMagicVehicle_Monorail(const Vehicle* vehicle);
    void AddLightsMagicVehicle_MiniatureRailway(const Vehicle* vehicle);

    void AddKioskLights(const CoordsXY& mapPosition, int32_t height, uint8_t zOffset);
    void AddShopLights(const CoordsXY& mapPosition, uint8_t direction, int32_t height, uint8_t zOffset);

    void ApplyPaletteFilter(uint8_t i, uint8_t* r, uint8_t* g, uint8_t* b);
    void RenderToTexture(
        const Viewport& vp, void* dstPixels, uint32_t dstPitch, Drawing::PaletteIndex* bits, uint32_t width, uint32_t height,
        const uint32_t* palette, const uint32_t* lightPalette);

} // namespace OpenRCT2::Drawing::LightFx
