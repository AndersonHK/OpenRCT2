/*****************************************************************************
 * Copyright (c) 2014-2026 OpenRCT2 developers
 *
 * For a complete list of all authors, please refer to contributors.md
 * Interested in contributing? Visit https://github.com/OpenRCT2/OpenRCT2
 *
 * OpenRCT2 is licensed under the GNU General Public License version 3.
 *****************************************************************************/

#include <algorithm>
#include <array>
#include <gtest/gtest.h>
#include <openrct2/OpenRCT2.h>
#include <openrct2/SpriteIds.h>
#include <openrct2/drawing/Colour.h>
#include <openrct2/drawing/Drawing.Sprite.h>
#include <openrct2/drawing/RenderTarget.h>
#include <openrct2/drawing/SpriteAssetDecoder.h>
#include <openrct2/peep/PeepAnimations.h>
#include <openrct2/ride/CarEntry.h>
#include <stdexcept>

using namespace OpenRCT2;
using namespace OpenRCT2::Drawing;

namespace
{
    G1Element Element(int16_t width, int16_t height, G1Flags flags = {})
    {
        G1Element element;
        element.width = width;
        element.height = height;
        element.xOffset = -27;
        element.yOffset = 19;
        element.flags = flags;
        return element;
    }

    // This oracle deliberately calls the frozen software implementation independently of the decoder.
    // It tests only full-resolution asset extraction, not scene placement, GPU rasterization or final colour.
    void ExpectSoftwareBytes(G1Element element, std::vector<uint8_t> data, std::span<PaletteIndex> remap = {})
    {
        element.offset = data.data();
        std::vector<PaletteIndex> reference(static_cast<size_t>(element.width) * element.height);
        RenderTarget target{};
        target.bits = reference.data();
        target.width = element.width;
        target.height = element.height;
        const auto image = remap.empty() ? ImageId(0) : ImageId(0, Colour::black);
        const auto palette = remap.empty() ? PaletteMap::GetDefault() : PaletteMap(remap);
        DrawSpriteArgs args(image, palette, element, 0, 0, element.width, element.height, reference.data());
        GfxSpriteToBuffer(target, args);

        const auto bounded = DecodeSpriteAsset(element, data, remap);
        const auto trusted = DecodeTrustedSpriteAsset(element, remap);
        EXPECT_EQ(bounded.pixels, reference);
        EXPECT_EQ(trusted.pixels, reference);
        EXPECT_EQ(bounded.coverage, trusted.coverage);

        // Lookup-table assembly also needs to know whether an encoded zero replaces an existing entry.
        // Verify coverage against software over nonzero storage, independently of the zero-filled atlas case.
        const auto background = static_cast<PaletteIndex>(254);
        std::fill(reference.begin(), reference.end(), background);
        GfxSpriteToBuffer(target, args);
        for (size_t i = 0; i < reference.size(); i++)
            EXPECT_EQ(bounded.coverage[i] != 0 ? bounded.pixels[i] : background, reference[i]) << "index " << i;
    }

    std::array<PaletteIndex, 256> MakeRemap()
    {
        std::array<PaletteIndex, 256> remap{};
        for (size_t i = 0; i < remap.size(); i++)
            remap[i] = static_cast<PaletteIndex>((i * 73) & 255);
        remap[0] = static_cast<PaletteIndex>(45); // Source zero must remain transparent even when remapped.
        remap[9] = PaletteIndex::transparent;
        return remap;
    }

    std::vector<uint8_t> EncodeRows(int16_t width, int16_t height, uint32_t seed)
    {
        std::vector<uint8_t> data(static_cast<size_t>(height) * 2);
        for (int32_t y = 0; y < height; y++)
        {
            data[y * 2] = static_cast<uint8_t>(data.size());
            data[y * 2 + 1] = static_cast<uint8_t>(data.size() >> 8);
            const auto first = static_cast<uint8_t>((seed + y) % 4);
            const auto count = static_cast<uint8_t>(std::min<int32_t>(width - first, 127));
            data.push_back(0x80 | count);
            data.push_back(first);
            for (int32_t x = 0; x < count; x++)
                data.push_back(static_cast<uint8_t>(seed * 17 + x * 31 + y * 7));
        }
        return data;
    }
} // namespace

TEST(SpriteAssetDecoderTest, BitmapCorpusMatchesFrozenSoftwareIndices)
{
    auto remap = MakeRemap();
    for (const int16_t width : { 1, 7, 16, 127, 256 })
    {
        for (const int16_t height : { 1, 3, 19 })
        {
            SCOPED_TRACE(::testing::Message() << width << 'x' << height);
            std::vector<uint8_t> data(static_cast<size_t>(width) * height);
            for (size_t i = 0; i < data.size(); i++)
                data[i] = static_cast<uint8_t>(i * 47);
            for (const auto flags : { G1Flags{}, G1Flags{ G1Flag::hasTransparency }, G1Flags{ G1Flag::one } })
            {
                ExpectSoftwareBytes(Element(width, height, flags), data);
                ExpectSoftwareBytes(Element(width, height, flags), data, remap);
            }
        }
    }
}

TEST(SpriteAssetDecoderTest, RleCorpusMatchesFrozenSoftwareIndices)
{
    auto remap = MakeRemap();
    for (const int16_t width : { 7, 17, 128, 256 })
    {
        for (const int16_t height : { 1, 9, 33 })
        {
            for (uint32_t seed = 0; seed < 16; seed++)
            {
                SCOPED_TRACE(::testing::Message() << width << 'x' << height << " seed " << seed);
                const auto data = EncodeRows(width, height, seed);
                const auto element = Element(width, height, { G1Flag::hasRLECompression });
                ExpectSoftwareBytes(element, data);
                ExpectSoftwareBytes(element, data, remap);
            }
        }
    }
}

TEST(SpriteAssetDecoderTest, RleRunsPreserveHolesOverlapsZeroBytesAndRightClipping)
{
    // First row: empty run, [0, 1, 9] at x=1, overlapping [2, 0] at x=2, then a clipped run.
    // Second row shares the same row offset; the third is empty, with an off-image start.
    const std::vector<uint8_t> data = {
        6, 0, 6, 0, 22, 0, 0, 0, 3, 1, 0, 1, 9, 2, 2, 2, 0, 0x83, 6, 3, 4, 5, 0x80, 255,
    };
    auto remap = MakeRemap();
    auto element = Element(7, 3, { G1Flag::hasRLECompression, G1Flag::one });
    ExpectSoftwareBytes(element, data);
    ExpectSoftwareBytes(element, data, remap);
    const auto decoded = DecodeSpriteAsset(element, data);
    EXPECT_EQ(decoded.coverage, (std::vector<uint8_t>{ 0, 1, 1, 1, 0, 0, 1, 0, 1, 1, 1, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0 }));
}

TEST(SpriteAssetDecoderTest, BitmapCoverageDistinguishesEncodedZeroFromTransparentZero)
{
    const std::vector<uint8_t> data{ 0, 1, 255, 0 };
    const auto raw = DecodeSpriteAsset(Element(4, 1), data);
    const auto transparent = DecodeSpriteAsset(Element(4, 1, { G1Flag::hasTransparency }), data);
    EXPECT_EQ(raw.pixels, transparent.pixels);
    EXPECT_EQ(raw.coverage, (std::vector<uint8_t>{ 1, 1, 1, 1 }));
    EXPECT_EQ(transparent.coverage, (std::vector<uint8_t>{ 0, 1, 1, 0 }));
}

TEST(SpriteAssetDecoderTest, GlyphRemapKeepsSourceAndMappedZeroTransparent)
{
    std::array<PaletteIndex, 8> remap{};
    remap[0] = static_cast<PaletteIndex>(45);
    remap[1] = static_cast<PaletteIndex>(19);
    remap[3] = static_cast<PaletteIndex>(99);
    const std::vector<uint8_t> data{ 0, 1, 2, 3, 4, 5, 6, 7 };
    const auto decoded = DecodeSpriteAsset(Element(8, 1), data, remap);
    ExpectSoftwareBytes(Element(8, 1), data, remap);
    EXPECT_EQ(decoded.coverage, (std::vector<uint8_t>{ 0, 1, 0, 1, 0, 0, 0, 0 }));
}

TEST(SpriteAssetDecoderTest, RejectsTruncatedBitmapAndRleData)
{
    const auto bitmap = Element(2, 2);
    const auto rle = Element(2, 1, { G1Flag::hasRLECompression });
    for (const auto data : { std::vector<uint8_t>{}, std::vector<uint8_t>{ 1, 2, 3 } })
        EXPECT_THROW(static_cast<void>(DecodeSpriteAsset(bitmap, data)), std::invalid_argument);
    for (const auto data : {
             std::vector<uint8_t>{},
             std::vector<uint8_t>{ 2 },
             std::vector<uint8_t>{ 255, 255 },
             std::vector<uint8_t>{ 2, 0, 0x82 },
             std::vector<uint8_t>{ 2, 0, 0x82, 0, 1 },
             std::vector<uint8_t>{ 2, 0, 0, 0 },
         })
        EXPECT_THROW(static_cast<void>(DecodeSpriteAsset(rle, data)), std::invalid_argument);
}

TEST(SpriteAssetDecoderTest, RejectsInvalidDimensionsRgbPaletteAndShortRemap)
{
    const std::vector<uint8_t> data{ 3 };
    std::array<PaletteIndex, 2> remap{};
    EXPECT_THROW(static_cast<void>(DecodeSpriteAsset(Element(-1, 1), data)), std::invalid_argument);
    EXPECT_THROW(static_cast<void>(DecodeSpriteAsset(Element(1, -1), data)), std::invalid_argument);
    EXPECT_THROW(static_cast<void>(DecodeSpriteAsset(Element(1, 1, { G1Flag::isPalette }), data)), std::invalid_argument);
    EXPECT_THROW(static_cast<void>(DecodeSpriteAsset(Element(1, 1), data, remap)), std::invalid_argument);
    EXPECT_THROW(static_cast<void>(DecodeTrustedSpriteAsset(Element(1, 1))), std::invalid_argument);
}

TEST(SpriteAssetDecoderTest, EmptyAndSuppressedBitmapAssetsDoNotReadSource)
{
    EXPECT_TRUE(DecodeSpriteAsset(Element(0, 1), {}).pixels.empty());
    EXPECT_TRUE(DecodeSpriteAsset(Element(1, 0), {}).pixels.empty());
    const auto decoded = DecodeSpriteAsset(Element(2, 2, { G1Flag::one }), {});
    EXPECT_EQ(decoded.coverage, (std::vector<uint8_t>{ 0, 0, 0, 0 }));
}

namespace
{
    struct TemporaryMetadataSprites
    {
        bool oldNoGraphics = gOpenRCT2NoGraphics;
        std::array<G1Element, 2> saved;
        TemporaryMetadataSprites()
        {
            gOpenRCT2NoGraphics = false;
            for (size_t i = 0; i < saved.size(); i++)
                saved[i] = *GfxGetG1Element(SPR_TEMP_BEGIN + static_cast<uint32_t>(i));
        }
        ~TemporaryMetadataSprites()
        {
            for (size_t i = 0; i < saved.size(); i++)
                GfxSetG1Element(SPR_TEMP_BEGIN + static_cast<uint32_t>(i), &saved[i]);
            gOpenRCT2NoGraphics = oldNoGraphics;
        }
    };

    InferredSpriteAssetBounds FrozenSoftwareBounds(uint32_t count)
    {
        std::array<PaletteIndex, 200 * 200> pixels{};
        RenderTarget target{};
        target.bits = pixels.data();
        target.x = -100;
        target.y = -100;
        target.width = 200;
        target.height = 200;
        for (uint32_t i = 0; i < count; i++)
            GfxDrawSpriteSoftware(target, ImageId(SPR_TEMP_BEGIN + i), { 0, 0 });
        InferredSpriteAssetBounds bounds{};
        // Independent copy of the original metadata scan, over frozen software output.
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

    void ExpectMetadataMatchesSoftware(const G1Element& first, const G1Element& second)
    {
        GfxSetG1Element(SPR_TEMP_BEGIN, &first);
        GfxSetG1Element(SPR_TEMP_BEGIN + 1, &second);
        const auto expected = FrozenSoftwareBounds(2);
        SpriteAssetBoundsAccumulator inference;
        inference.Add(first);
        inference.Add(second);
        const auto actual = inference.GetBounds();
        EXPECT_EQ(actual.width, expected.width);
        EXPECT_EQ(actual.heightNegative, expected.heightNegative);
        EXPECT_EQ(actual.heightPositive, expected.heightPositive);
        PeepAnimation animation{};
        animation.baseImage = SPR_TEMP_BEGIN;
        animation.frameOffsets = { 0, 1 };
        const auto peep = inferMaxAnimationDimensions(animation);
        EXPECT_EQ(peep.spriteWidth, expected.width);
        EXPECT_EQ(peep.spriteHeightNegative, expected.heightNegative);
        EXPECT_EQ(peep.spriteHeightPositive, expected.heightPositive);
        CarEntry car{};
        car.baseImageId = SPR_TEMP_BEGIN;
        CarEntrySetImageMaxSizes(car, 2);
        EXPECT_EQ(car.spriteWidth, expected.width);
        EXPECT_EQ(car.spriteHeightNegative, expected.heightNegative);
        EXPECT_EQ(car.spriteHeightPositive, expected.heightPositive);
        car.flags.set(CarEntryFlag::spriteBoundsIncludeInvertedSet);
        CarEntrySetImageMaxSizes(car, 2);
        EXPECT_EQ(car.spriteHeightNegative, expected.heightNegative + 16);
    }
} // namespace

TEST(SpriteAssetDecoderTest, MetadataBoundsMatchFrozenSoftwareClippingAndOrderedZeroClearing)
{
    TemporaryMetadataSprites restore;
    std::vector<uint8_t> filled(17 * 13, 37);
    std::vector<uint8_t> zeros(17 * 13);
    auto rle = EncodeRows(17, 13, 0);
    // Turn encoded values into zero, retaining actual RLE holes.
    for (size_t row = 0; row < 13; row++)
    {
        const auto offset = static_cast<size_t>(rle[row * 2]) | (static_cast<size_t>(rle[row * 2 + 1]) << 8);
        std::fill_n(rle.begin() + offset + 2, rle[offset] & 127, 0);
    }
    for (const int16_t x : { -110, -100, -99, -17, -1, 0, 1, 90, 99, 100 })
    {
        for (const int16_t y : { -110, -100, -99, -17, -1, 0, 1, 90, 99, 100 })
        {
            for (const auto flags : { G1Flags{}, G1Flags{ G1Flag::hasTransparency }, G1Flags{ G1Flag::one },
                                      G1Flags{ G1Flag::hasRLECompression } })
            {
                SCOPED_TRACE(::testing::Message() << "offset " << x << ',' << y);
                auto first = Element(17, 13);
                first.offset = filled.data();
                first.xOffset = x;
                first.yOffset = y;
                auto second = first;
                second.flags = flags;
                second.offset = flags.has(G1Flag::hasRLECompression) ? rle.data() : zeros.data();
                ExpectMetadataMatchesSoftware(first, second);
            }
        }
    }
}

TEST(SpriteAssetDecoderTest, MetadataBoundsPreserveCentreAndFirstRowColumnExclusions)
{
    TemporaryMetadataSprites restore;
    std::vector<uint8_t> point{ 31 };
    auto empty = Element(0, 0);
    for (const int16_t x : { -101, -100, -99, -1, 0, 1, 99, 100 })
    {
        for (const int16_t y : { -101, -100, -99, -1, 0, 1, 99, 100 })
        {
            auto element = Element(1, 1);
            element.offset = point.data();
            element.xOffset = x;
            element.yOffset = y;
            ExpectMetadataMatchesSoftware(element, empty);
        }
    }
}

TEST(SpriteAssetDecoderTest, ScrollingGlyphMaskMatchesFrozenSoftwareOffsetsAndFormats)
{
    TemporaryMetadataSprites restore;
    std::vector<uint8_t> bitmap(17 * 13);
    for (size_t i = 0; i < bitmap.size(); i++)
        bitmap[i] = static_cast<uint8_t>(i % 5 == 0 ? PaletteIndex::fontFill : PaletteIndex::transparent);
    auto rle = EncodeRows(17, 13, 0);
    for (size_t row = 0; row < 13; row++)
    {
        const auto offset = static_cast<size_t>(rle[row * 2]) | (static_cast<size_t>(rle[row * 2 + 1]) << 8);
        for (size_t x = 0; x < static_cast<size_t>(rle[offset] & 127); x++)
            rle[offset + 2 + x] = static_cast<uint8_t>(x % 3 == 0 ? PaletteIndex::fontFill : PaletteIndex::transparent);
    }
    for (const int16_t x : { -10, -1, 0, 1, 7, 8 })
    {
        for (const int16_t y : { -10, -1, 0, 1, 7, 8 })
        {
            for (const auto flags : { G1Flags{}, G1Flags{ G1Flag::hasTransparency }, G1Flags{ G1Flag::one },
                                      G1Flags{ G1Flag::hasRLECompression } })
            {
                auto element = Element(17, 13, flags);
                element.offset = flags.has(G1Flag::hasRLECompression) ? rle.data() : bitmap.data();
                element.xOffset = x;
                element.yOffset = y;
                GfxSetG1Element(SPR_TEMP_BEGIN, &element);
                std::array<PaletteIndex, 64> reference{};
                RenderTarget target{};
                target.bits = reference.data();
                target.width = 8;
                target.height = 8;
                GfxDrawSpriteSoftware(target, ImageId(SPR_TEMP_BEGIN), { -1, 0 });
                const auto mask = ExtractScrollingGlyphMask(element);
                for (size_t column = 0; column < 8; column++)
                    for (size_t row = 0; row < 8; row++)
                        EXPECT_EQ((mask[column] & (1U << row)) != 0, reference[row * 8 + column] == PaletteIndex::fontFill);
            }
        }
    }
}
