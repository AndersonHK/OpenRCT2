/*****************************************************************************
 * Copyright (c) 2014-2026 OpenRCT2 developers
 * OpenRCT2 is licensed under the GNU General Public License version 3.
 *****************************************************************************/
#include "GpuGraphicsLookupTables.h"

#include "GpuTextureCache.h"

#include <algorithm>
#include <cstring>
#include <openrct2/drawing/BlendColourMap.h>
#include <openrct2/drawing/Drawing.Sprite.h>
#include <openrct2/drawing/Drawing.h>
#include <openrct2/drawing/G1Element.h>
#include <openrct2/drawing/Palette.h>
#include <openrct2/drawing/SpriteAssetDecoder.h>

namespace OpenRCT2::Ui::Gpu
{
    [[nodiscard]] std::array<std::byte, 256 * 256> BuildRemapPalette()
    {
        std::array<Drawing::PaletteIndex, 256 * 256> indices{};
        for (int32_t i = 0; i < 256; i++)
            indices[i] = static_cast<Drawing::PaletteIndex>(i);

        for (int32_t i = 0; i < kPaletteTotalOffsets; i++)
        {
            const auto palette = static_cast<Drawing::FilterPaletteID>(i);
            const auto image = GetPaletteG1Index(palette);
            if (!image.has_value())
                continue;
            const auto* element = GfxGetG1Element(*image);
            if (element == nullptr)
                continue;
            const int32_t row = Gpu::TextureCache::PaletteToY(palette);
            const auto decoded = Drawing::DecodeTrustedSpriteAsset(*element);
            // These indexed assets contain lookup rows (the water asset has several). Asset offsets
            // describe sprite placement and do not affect table coordinates. Preserve holes if rows overlap.
            const auto width = std::min<int32_t>(element->width, 256);
            const auto height = std::min<int32_t>(element->height, 256 - row);
            for (int32_t y = 0; y < height; y++)
            {
                for (int32_t x = 0; x < width; x++)
                {
                    const auto source = static_cast<size_t>(y) * element->width + x;
                    if (decoded.coverage[source] != 0)
                        indices[(row + y) * 256 + x] = decoded.pixels[source];
                }
            }
        }
        std::array<std::byte, 256 * 256> pixels{};
        std::memcpy(pixels.data(), indices.data(), pixels.size());
        return pixels;
    }

    GraphicsLookupTables CaptureGraphicsLookupTables()
    {
        GraphicsLookupTables result;
        result.remap = BuildRemapPalette();
        if (const auto* blend = Drawing::GetBlendColourMap())
        {
            static_assert(sizeof(*blend) == sizeof(result.blend));
            std::memcpy(result.blend.data(), blend, result.blend.size());
            result.hasBlend = true;
        }
        return result;
    }
} // namespace OpenRCT2::Ui::Gpu
