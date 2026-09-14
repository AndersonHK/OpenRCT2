/*****************************************************************************
 * Copyright (c) 2014-2026 OpenRCT2 developers
 *
 * For a complete list of all authors, please refer to contributors.md
 * Interested in contributing? Visit https://github.com/OpenRCT2/OpenRCT2
 *
 * OpenRCT2 is licensed under the GNU General Public License version 3.
 *****************************************************************************/

#include "TestData.h"

#include <gtest/gtest.h>
#include <openrct2/core/Path.hpp>
#include <openrct2/drawing/ImageImporter.h>
#include <stdexcept>
#include <string_view>

using namespace OpenRCT2;
using namespace OpenRCT2::Drawing;

class ImageImporterTests : public testing::Test
{
public:
    static std::string GetImagePath(const std::string& name)
    {
        return Path::Combine(TestData::GetBasePath(), u8"images", name.c_str());
    }

    static uint32_t GetHash(void* buffer, size_t bufferLength)
    {
        uint32_t hash = 27;
        for (size_t i = 0; i < bufferLength; i++)
        {
            hash = (13 * hash) + (reinterpret_cast<uint8_t*>(buffer))[i];
        }
        return hash;
    }
};

TEST_F(ImageImporterTests, Import_Logo)
{
    auto logoPath = GetImagePath("logo.png");

    ImageImporter importer;
    auto image = Imaging::ReadFromFile(logoPath, ImageFormat::png32);
    auto meta = ImageImportMeta{ .offset = { 3, 5 } };
    auto result = importer.Import(image, meta);

    ASSERT_EQ(result.Buffer.data(), result.Element.offset);
    ASSERT_EQ(128, result.Element.width);
    ASSERT_EQ(128, result.Element.height);
    ASSERT_EQ(3, result.Element.xOffset);
    ASSERT_EQ(5, result.Element.yOffset);
    ASSERT_EQ(0, result.Element.zoomedOffset);

    // Check to ensure RLE data doesn't change unexpectedly.
    // Update expected hash if change is expected.
    ASSERT_NE(nullptr, result.Buffer.data());
    auto hash = GetHash(result.Buffer.data(), result.Buffer.size());
    ASSERT_EQ(uint32_t(0x212A99BC), hash);
}

TEST_F(ImageImporterTests, InvalidPngFormatsThrowAndValidImportsStillWork)
{
    // RGBA and grayscale+alpha have more than one byte per pixel and cannot keep palette indices.
    for (const auto* name : { "rgba-1x1.png", "grayscale-alpha-1x1.png" })
    {
        SCOPED_TRACE(name);
        EXPECT_THROW(Imaging::ReadFromFile(GetImagePath(name), ImageFormat::png), std::runtime_error);
        const auto image = Imaging::ReadFromFile(GetImagePath(name), ImageFormat::png32);
        EXPECT_EQ(image.Width, 1u);
        EXPECT_EQ(image.Height, 1u);
        EXPECT_EQ(image.Depth, 32u);
        EXPECT_EQ(image.Pixels, (std::vector<uint8_t>{ 255, 255, 255, 255 }));
    }
    EXPECT_THROW(Imaging::ReadFromBuffer({ 0, 1, 2, 3, 4, 5, 6, 7 }, ImageFormat::png32), std::runtime_error);
    auto image = Imaging::ReadFromFile(GetImagePath("rgba-1x1.png"), ImageFormat::png32);
    ImageImporter importer;
    ImageImportMeta meta{};
    EXPECT_NO_THROW(importer.Import(image, meta));
}

TEST_F(ImageImporterTests, OversizedPngThrowsInvalidArgumentAndAllowsSubsequentImport)
{
    ImageImporter importer;
    for (const auto* name : { "rgba-301x1.png", "rgba-1x301.png" })
    {
        SCOPED_TRACE(name);
        auto image = Imaging::ReadFromFile(GetImagePath(name), ImageFormat::png32);
        ImageImportMeta meta{};
        EXPECT_THROW(importer.Import(image, meta), std::invalid_argument);
    }
    auto image = Imaging::ReadFromFile(GetImagePath("rgba-1x1.png"), ImageFormat::png32);
    ImageImportMeta meta{};
    const auto result = importer.Import(image, meta);
    EXPECT_EQ(result.Element.width, 1);
    EXPECT_EQ(result.Element.height, 1);
    EXPECT_FALSE(result.Buffer.empty());
}
