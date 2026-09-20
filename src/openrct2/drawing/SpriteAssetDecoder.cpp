/*****************************************************************************
 * Copyright (c) 2014-2026 OpenRCT2 developers
 *
 * For a complete list of all authors, please refer to contributors.md
 * Interested in contributing? Visit https://github.com/OpenRCT2/OpenRCT2
 *
 * OpenRCT2 is licensed under the GNU General Public License version 3.
 *****************************************************************************/

#include "SpriteAssetDecoder.h"

#include "G1Element.h"

#include <algorithm>
#include <optional>
#include <stdexcept>

namespace OpenRCT2::Drawing
{
    namespace
    {
        struct AssetBytes
        {
            const uint8_t* data;
            std::optional<size_t> size;

            void Require(size_t offset, size_t length) const
            {
                if (data == nullptr || (size.has_value() && (offset > *size || length > *size - offset)))
                    throw std::invalid_argument("Truncated sprite asset");
            }
        };

        DecodedSpriteAsset Decode(const G1Element& element, const AssetBytes& source, std::span<const PaletteIndex> remap)
        {
            if (element.width < 0 || element.height < 0 || element.flags.has(G1Flag::isPalette))
                throw std::invalid_argument("Sprite decoder requires nonnegative indexed image dimensions");
            const auto width = static_cast<size_t>(element.width);
            const auto height = static_cast<size_t>(element.height);
            DecodedSpriteAsset result{ std::vector<PaletteIndex>(width * height, PaletteIndex::transparent),
                                       std::vector<uint8_t>(width * height) };
            if (width == 0 || height == 0)
                return result;

            const bool rle = element.flags.has(G1Flag::hasRLECompression);
            // The legacy asset flag suppresses bitmap data, but does not suppress RLE data.
            if (!rle && element.flags.has(G1Flag::one))
                return result;

            const auto unpack = [&](size_t destination, uint8_t value) {
                auto index = static_cast<PaletteIndex>(value);
                if (!remap.empty())
                {
                    if (value == 0)
                        return;
                    if (value >= remap.size())
                        throw std::invalid_argument("Sprite asset exceeds its source remap table");
                    index = remap[value];
                    if (index == PaletteIndex::transparent)
                        return;
                }
                else if (!rle && element.flags.has(G1Flag::hasTransparency) && value == 0)
                {
                    return;
                }
                result.pixels[destination] = index;
                result.coverage[destination] = 1;
            };

            if (!rle)
            {
                source.Require(0, width * height);
                for (size_t i = 0; i < result.pixels.size(); i++)
                    unpack(i, source.data[i]);
                return result;
            }

            source.Require(0, height * 2);
            for (size_t y = 0; y < height; y++)
            {
                size_t cursor = source.data[y * 2] | (static_cast<size_t>(source.data[y * 2 + 1]) << 8);
                bool last = false;
                do
                {
                    source.Require(cursor, 2);
                    const auto header = source.data[cursor];
                    const auto start = static_cast<size_t>(source.data[cursor + 1]);
                    const auto length = static_cast<size_t>(header & 0x7F);
                    last = (header & 0x80) != 0;
                    cursor += 2;
                    source.Require(cursor, length);
                    // Some asset runs extend past their declared width. Decode only the represented rectangle,
                    // while still consuming the entire run, matching the asset extraction used by the atlas.
                    const auto count = start < width ? std::min(length, width - start) : 0;
                    for (size_t x = 0; x < count; x++)
                        unpack(y * width + start + x, source.data[cursor + x]);
                    cursor += length;
                } while (!last);
            }
            return result;
        }
    } // namespace

    DecodedSpriteAsset DecodeSpriteAsset(
        const G1Element& element, std::span<const uint8_t> data, std::span<const PaletteIndex> remap)
    {
        return Decode(element, { data.data(), data.size() }, remap);
    }

    DecodedSpriteAsset DecodeTrustedSpriteAsset(const G1Element& element, std::span<const PaletteIndex> remap)
    {
        return Decode(element, { element.offset, std::nullopt }, remap);
    }

    void SpriteAssetBoundsAccumulator::Add(const G1Element& element)
    {
        const auto asset = DecodeTrustedSpriteAsset(element);
        const auto left = std::max<int32_t>(0, -100 - element.xOffset);
        const auto top = std::max<int32_t>(0, -100 - element.yOffset);
        const auto right = std::min<int32_t>(element.width, 100 - element.xOffset);
        const auto bottom = std::min<int32_t>(element.height, 100 - element.yOffset);
        for (auto y = top; y < bottom; y++)
        {
            for (auto x = left; x < right; x++)
            {
                const auto source = static_cast<size_t>(y) * element.width + x;
                if (asset.coverage[source] != 0)
                {
                    const auto destination = static_cast<size_t>(y + element.yOffset + 100) * 200 + x + element.xOffset + 100;
                    _occupied[destination] = asset.pixels[source] != PaletteIndex::transparent;
                }
            }
        }
    }

    InferredSpriteAssetBounds SpriteAssetBoundsAccumulator::GetBounds() const
    {
        InferredSpriteAssetBounds bounds{};
        for (int32_t y = 0; y < 200; y++)
        {
            for (int32_t x = 0; x < 200; x++)
            {
                if (!_occupied[y * 200 + x])
                    continue;
                // Preserve the original metadata scans: column/row zero and the centre do not
                // contribute along their own axis. These asymmetries affect legacy culling bounds.
                if (x > 0 && x != 100)
                    bounds.width = std::max(bounds.width, static_cast<uint8_t>((x < 100 ? 100 - x : x - 100) + 1));
                if (y > 0 && y < 100)
                    bounds.heightNegative = std::max(bounds.heightNegative, static_cast<uint8_t>(101 - y));
                if (y > 100)
                    bounds.heightPositive = std::max(bounds.heightPositive, static_cast<uint8_t>(y - 99));
            }
        }
        return bounds;
    }

    std::array<uint8_t, 8> ExtractScrollingGlyphMask(const G1Element& element)
    {
        const auto asset = DecodeTrustedSpriteAsset(element);
        std::array<uint8_t, 8> columns{};
        for (int32_t x = 0; x < 8; x++)
        {
            const auto sourceX = x + 1 - element.xOffset;
            if (sourceX < 0 || sourceX >= element.width)
                continue;
            for (int32_t y = 0; y < 8; y++)
            {
                const auto sourceY = y - element.yOffset;
                if (sourceY >= 0 && sourceY < element.height
                    && asset.pixels[static_cast<size_t>(sourceY) * element.width + sourceX] == PaletteIndex::fontFill)
                {
                    columns[x] |= static_cast<uint8_t>(1U << y);
                }
            }
        }
        return columns;
    }
} // namespace OpenRCT2::Drawing
