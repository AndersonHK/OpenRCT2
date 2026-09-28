/*****************************************************************************
 * Copyright (c) 2014-2026 OpenRCT2 developers
 * OpenRCT2 is licensed under the GNU General Public License version 3.
 *****************************************************************************/

#include "VulkanParityTestSupport.h"

#include <cstdlib>
#include <gtest/gtest.h>
#include <limits>
#include <openrct2/Context.h>
#include <openrct2/OpenRCT2.h>
#include <openrct2/PlatformEnvironment.h>
#include <openrct2/config/Config.h>
#include <openrct2/drawing/Drawing.Sprite.h>
#include <openrct2/drawing/Drawing.h>
#include <openrct2/drawing/Palette.h>
#include <openrct2/drawing/RenderTarget.h>
#include <openrct2/drawing/SpriteAssetDecoder.h>
#include <openrct2/interface/Viewport.h>
#include <openrct2/object/ObjectRepository.h>
#include <openrct2/object/PeepAnimationsObject.h>
#include <openrct2/object/RideObject.h>
#include <openrct2/rct1/Csg.h>
#ifdef ENABLE_SCRIPTING
    #include <openrct2/scripting/ScriptEngine.h>
#endif

namespace
{
    using namespace OpenRCT2;
    using namespace OpenRCT2::Drawing;
    using namespace VulkanParitySupport;

    struct LoadedObject
    {
        std::unique_ptr<Object> object;
        bool started = false;
        ~LoadedObject()
        {
            // Load can allocate image/string resources before throwing.
            if (object != nullptr && started)
                object->Unload();
        }
        void Load()
        {
            started = true;
            object->Load();
        }
    };

    std::array<uint8_t, 3> FrozenBounds(const std::vector<PaletteIndex>& pixels)
    {
        std::array<uint8_t, 3> result{};
        for (int32_t distance = 99; distance != 0; distance--)
            for (int32_t across = 0; across < 200; across++)
            {
                if (result[0] == 0
                    && (pixels[across * 200 + 100 - distance] != PaletteIndex::transparent
                        || pixels[across * 200 + 100 + distance] != PaletteIndex::transparent))
                    result[0] = static_cast<uint8_t>(distance + 1);
                if (result[1] == 0 && pixels[(100 - distance) * 200 + across] != PaletteIndex::transparent)
                    result[1] = static_cast<uint8_t>(distance + 1);
                if (result[2] == 0 && pixels[(100 + distance) * 200 + across] != PaletteIndex::transparent)
                    result[2] = static_cast<uint8_t>(distance + 1);
            }
        return result;
    }

    class AssetMetadataCorpusParityTest : public testing::Test
    {
    protected:
        const bool oldHeadless = gOpenRCT2Headless;
        const bool oldNoGraphics = gOpenRCT2NoGraphics;
        const u8string oldRct1 = Config::Get().general.rct1Path;
        const u8string oldRct2 = Config::Get().general.rct2Path;
        const uint8_t oldThreading = Config::Get().general.multiThreading.load();
        const GamePalette oldPalette = gPalette;
        const GamePalette oldGamePalette = gGamePalette;
        const uint32_t oldPaletteFrame = gPaletteEffectFrame;
        std::unique_ptr<IContext> context;
        bool contextReady = false;
        std::filesystem::path artifacts;
        json_t report = {
            { "fixtureVersion", 2 }, { "objects", json_t::array() }, { "missingSpriteCount", 0 }, { "foreignSpriteCount", 0 }
        };

        void SetUp() override
        {
            const auto* rct2 = std::getenv("OPENRCT2_TEST_RCT2_PATH");
            const auto* rct1 = std::getenv("OPENRCT2_TEST_RCT1_PATH");
            if (rct2 == nullptr || rct1 == nullptr)
            {
                const auto* required = std::getenv("OPENRCT2_REQUIRE_VULKAN_TESTS");
                if (required != nullptr && std::string_view(required) == "1")
                    FAIL() << "Full metadata corpus requires pinned RCT1 and RCT2 paths";
                GTEST_SKIP() << "Full metadata corpus requires OPENRCT2_TEST_RCT1_PATH and OPENRCT2_TEST_RCT2_PATH";
            }
            ASSERT_EQ(GetContext(), nullptr);
            std::filesystem::path data = std::filesystem::current_path() / "data";
            if (const auto* shaders = std::getenv("OPENRCT2_VULKAN_SHADER_DIRECTORY"))
                data = std::filesystem::path(shaders).parent_path().parent_path();
            else if (!std::filesystem::is_regular_file(data / "g2.dat"))
                data = std::filesystem::current_path() / "bin/data";
            for (const auto* name : { "g2.dat", "palettes.dat", "fonts.dat", "tracks.dat" })
            {
                ASSERT_TRUE(std::filesystem::is_regular_file(data / name)) << name;
                report["assetHashes"][name] = HashFile(data / name);
            }
            if (const auto* path = std::getenv("OPENRCT2_VULKAN_PARITY_ARTIFACTS"))
                artifacts = path;
            gOpenRCT2Headless = true;
            gOpenRCT2NoGraphics = false;
            context = CreateContext();
            // CreateContext reloads Config from the active profile. Pin asset inputs
            // afterwards so preceding tests/profile contents cannot select fallback art.
            Config::Get().general.rct1Path = rct1;
            Config::Get().general.rct2Path = rct2;
            Config::Get().general.multiThreading = false;
            auto& env = context->GetPlatformEnvironment();
            env.SetBasePath(DirBase::openrct2, data.string());
            env.SetBasePath(DirBase::rct1, rct1);
            env.SetBasePath(DirBase::rct2, rct2);
            const auto g1 = env.FindFile(DirBase::rct2, DirId::data, "g1.dat");
            ASSERT_FALSE(g1.empty());
            report["assetHashes"]["g1.dat"] = HashFile(g1);
            ASSERT_EQ(Config::Get().general.rct1Path, rct1);
            ASSERT_EQ(Config::Get().general.rct2Path, rct2);
            ASSERT_TRUE(context->Initialise());
            contextReady = true;
            ASSERT_TRUE(IsCsgLoaded()) << "RCT1 fallback images are not accepted for this corpus";
            report["assetHashes"]["csg1.dat"] = HashFile(FindCsg1datAtLocation(rct1));
            report["assetHashes"]["csg1i.dat"] = HashFile(FindCsg1idatAtLocation(rct1));
        }

        void TearDown() override
        {
            if (context != nullptr && context->GetDrawingEngine() != nullptr)
                ViewportDisposePresentation();
#ifdef ENABLE_SCRIPTING
            if (context != nullptr && !contextReady)
            {
                // Failed context startup may precede script class registration. Give this
                // context its own valid lifecycle before destroying it after another fixture.
                try
                {
                    context->GetScriptEngine().Initialise();
                }
                catch (const std::runtime_error& error)
                {
                    EXPECT_STREQ(error.what(), "Script engine already initialised.");
                }
            }
#endif
            context.reset();
            gOpenRCT2Headless = oldHeadless;
            gOpenRCT2NoGraphics = oldNoGraphics;
            Config::Get().general.rct1Path = oldRct1;
            Config::Get().general.rct2Path = oldRct2;
            Config::Get().general.multiThreading = oldThreading;
            gPalette = oldPalette;
            gGamePalette = oldGamePalette;
            gPaletteEffectFrame = oldPaletteFrame;
            if (!artifacts.empty())
            {
                std::filesystem::create_directories(artifacts / "AssetMetadataCorpus");
                report["passed"] = !HasFailure();
                std::ofstream stream(
                    artifacts / "AssetMetadataCorpus"
                    / (std::string(::testing::UnitTest::GetInstance()->current_test_info()->name()) + ".json"));
                ASSERT_TRUE(stream.good());
                stream << report.dump(2);
            }
        }

        std::vector<const ObjectRepositoryItem*> Objects(ObjectType type)
        {
            auto& repository = context->GetObjectRepository();
            std::vector<const ObjectRepositoryItem*> result;
            for (size_t i = 0; i < repository.GetNumObjects(); i++)
                if (repository.GetObjects()[i].Type == type)
                    result.push_back(&repository.GetObjects()[i]);
            std::sort(result.begin(), result.end(), [](const auto* a, const auto* b) {
                return std::tie(a->Identifier, a->Path) < std::tie(b->Identifier, b->Path);
            });
            return result;
        }

        std::array<uint8_t, 3> CompareRange(const Object& object, ImageIndex first, uint32_t count, json_t& item)
        {
            if (uint64_t(first) + count > std::numeric_limits<ImageIndex>::max())
                throw std::runtime_error("Metadata sprite range overflows image indices");
            std::vector<PaletteIndex> reference(200 * 200), decodedCanvas(200 * 200);
            RenderTarget target{ .bits = reference.data(), .x = -100, .y = -100, .width = 200, .height = 200 };
            uint64_t decodedHash = 14695981039346656037ULL;
            item["outsideObjectImages"] = json_t::array();
            for (uint32_t i = 0; i < count; i++)
            {
                const auto image = first + i;
                const bool owned = image >= object.GetBaseImageId()
                    && uint64_t(image) < uint64_t(object.GetBaseImageId()) + object.GetNumImages();
                const auto* element = GfxGetG1Element(image);
                GfxDrawSpriteSoftware(target, ImageId(image), { 0, 0 });
                json_t outside{ { "image", image }, { "rangeOffset", i }, { "resolved", element != nullptr } };
                if (element == nullptr)
                {
                    if (owned)
                        throw std::runtime_error("Unavailable image inside loaded object allocation");
                    // Legacy inverted ranges can exceed object ownership. Match the real
                    // global lookup/null-skip contract without truncating either oracle.
                    outside["provenance"] = "global GfxGetG1Element returned null";
                    item["outsideObjectImages"].push_back(outside);
                    report["missingSpriteCount"] = report["missingSpriteCount"].get<size_t>() + 1;
                    continue;
                }
                if (element->flags.has(G1Flag::isPalette))
                    throw std::runtime_error("Non-sprite image in metadata range");
                const auto decoded = DecodeTrustedSpriteAsset(*element);
                uint64_t imageHash = 14695981039346656037ULL;
                for (size_t pixel = 0; pixel < decoded.pixels.size(); pixel++)
                {
                    decodedHash = (decodedHash ^ static_cast<uint8_t>(decoded.pixels[pixel])) * 1099511628211ULL;
                    decodedHash = (decodedHash ^ decoded.coverage[pixel]) * 1099511628211ULL;
                    imageHash = (imageHash ^ static_cast<uint8_t>(decoded.pixels[pixel])) * 1099511628211ULL;
                    imageHash = (imageHash ^ decoded.coverage[pixel]) * 1099511628211ULL;
                }
                if (!owned)
                {
                    outside["dimensions"] = { element->width, element->height };
                    outside["offset"] = { element->xOffset, element->yOffset };
                    outside["hasRleCompression"] = element->flags.has(G1Flag::hasRLECompression);
                    outside["hasTransparency"] = element->flags.has(G1Flag::hasTransparency);
                    outside["bitmapOneFlag"] = element->flags.has(G1Flag::one);
                    outside["decodedPixelsAndCoverageFnv1a64"] = imageHash;
                    outside["provenance"] = "global dynamic slot; owner not registered in repository";
                    auto& repository = context->GetObjectRepository();
                    for (size_t ownerIndex = 0; ownerIndex < repository.GetNumObjects(); ownerIndex++)
                    {
                        const auto& candidate = repository.GetObjects()[ownerIndex];
                        if (candidate.LoadedObject == nullptr)
                            continue;
                        const auto& owner = *candidate.LoadedObject;
                        if (image >= owner.GetBaseImageId()
                            && uint64_t(image) < uint64_t(owner.GetBaseImageId()) + owner.GetNumImages())
                        {
                            outside["provenance"] = candidate.Identifier;
                            outside["ownerImageOffset"] = image - owner.GetBaseImageId();
                            outside["ownerSourceFileFnv1a64"] = HashFile(candidate.Path);
                            break;
                        }
                    }
                    item["outsideObjectImages"].push_back(outside);
                    report["foreignSpriteCount"] = report["foreignSpriteCount"].get<size_t>() + 1;
                }
                for (int32_t y = std::max<int32_t>(0, -100 - element->yOffset);
                     y < std::min<int32_t>(element->height, 100 - element->yOffset); y++)
                    for (int32_t x = std::max<int32_t>(0, -100 - element->xOffset);
                         x < std::min<int32_t>(element->width, 100 - element->xOffset); x++)
                    {
                        const auto source = y * element->width + x;
                        if (decoded.coverage[source] != 0)
                            decodedCanvas[(y + element->yOffset + 100) * 200 + x + element->xOffset + 100] = decoded.pixels
                                                                                                                 [source];
                    }
            }
            item["imageCount"] = count;
            item["firstImageOffset"] = first - object.GetBaseImageId();
            item["decodedPixelsAndCoverageFnv1a64"] = decodedHash;
            const auto expected = FrozenBounds(reference);
            item["frozenBounds"] = expected;
            const bool exactPixels = reference == decodedCanvas;
            item["exactClippedPixels"] = exactPixels;
            EXPECT_TRUE(exactPixels);
            if (!exactPixels && !artifacts.empty())
            {
                std::array<std::byte, 1024> palette{};
                for (size_t i = 0; i < 256; i++)
                {
                    palette[i * 4] = palette[i * 4 + 1] = palette[i * 4 + 2] = static_cast<std::byte>(i);
                    palette[i * 4 + 3] = std::byte{ 255 };
                }
                const auto name = item["sample"].get<std::string>();
                (void)CompareAndReport(
                    artifacts / "AssetMetadataCorpus", name, "indexed", std::as_bytes(std::span(reference)),
                    std::as_bytes(std::span(decodedCanvas)), 1, palette, item, { 200, 200 });
            }
            return expected;
        }
    };
} // namespace

TEST_F(AssetMetadataCorpusParityTest, AllAvailableRideCars)
{
    const auto objects = Objects(ObjectType::ride);
    ASSERT_FALSE(objects.empty());
    size_t cars = 0;
    size_t predefinedCars = 0;
    for (const auto* entry : objects)
    {
        SCOPED_TRACE(entry->Identifier);
        json_t item{ { "identifier", entry->Identifier },
                     { "path", entry->Path },
                     { "sourceFileFnv1a64", HashFile(entry->Path) },
                     { "cars", json_t::array() } };
        try
        {
            LoadedObject loaded{ context->GetObjectRepository().LoadObject(entry, true) };
            if (!loaded.object)
                throw std::runtime_error("Object parse/image load failed");
            const auto beforeLoad = static_cast<const RideObject&>(*loaded.object).GetEntry();
            loaded.Load();
            const auto& ride = static_cast<const RideObject&>(*loaded.object).GetEntry();
            for (size_t slot = 0; slot < std::size(ride.Cars); slot++)
            {
                auto car = ride.Cars[slot];
                if (!car.groupEnabled(SpriteGroupType::slopeFlat) || car.numCarImages == 0)
                    continue;
                json_t record{ { "sample", entry->Identifier + "_car" + std::to_string(slot) }, { "slot", slot } };
                const auto& original = beforeLoad.Cars[slot];
                const std::array originalBounds{ original.spriteWidth, original.spriteHeightNegative,
                                                 original.spriteHeightPositive };
                const std::array loadedBounds{ car.spriteWidth, car.spriteHeightNegative, car.spriteHeightPositive };
                record["beforeLoadBounds"] = originalBounds;
                record["loadedBounds"] = loadedBounds;
                const bool inferredOnLoad = !car.flags.has(CarEntryFlag::recalculateSpriteBounds);
                record["inferredOnLoad"] = inferredOnLoad;
                if (!inferredOnLoad)
                {
                    // Despite its historical name, this flag bypasses loader inference.
                    // Virtual vehicles can prescribe bounds without a matching sprite range.
                    EXPECT_EQ(loadedBounds, originalBounds);
                    record["coverage"] = "predefined bounds retained; loader does not call sprite inference";
                    item["cars"].push_back(record);
                    predefinedCars++;
                    continue;
                }
                const bool inverted = car.flags.has(CarEntryFlag::spriteBoundsIncludeInvertedSet);
                const auto count = car.numCarImages * (car.numSeatingRows + 1) * (inverted ? 2 : 1);
                auto expected = CompareRange(*loaded.object, car.baseImageId, count, record);
                expected[1] = static_cast<uint8_t>(expected[1] + (inverted ? 16 : 0));
                CarEntrySetImageMaxSizes(car, static_cast<int32_t>(count));
                const std::array actual{ car.spriteWidth, car.spriteHeightNegative, car.spriteHeightPositive };
                EXPECT_EQ(actual, expected);
                EXPECT_EQ(loadedBounds, expected);
                record["actualBounds"] = actual;
                record["inverted"] = inverted;
                item["cars"].push_back(record);
                cars++;
            }
        }
        catch (const std::exception& error)
        {
            item["error"] = error.what();
            ADD_FAILURE() << error.what();
        }
        report["objects"].push_back(item);
    }
    report["objectCount"] = objects.size();
    report["carCount"] = cars + predefinedCars;
    report["inferredCarCount"] = cars;
    report["predefinedCarCount"] = predefinedCars;
    EXPECT_GT(cars, 1u);
}

TEST_F(AssetMetadataCorpusParityTest, AllAvailablePeepActions)
{
    const auto objects = Objects(ObjectType::peepAnimations);
    ASSERT_FALSE(objects.empty());
    size_t actions = 0;
    for (const auto* entry : objects)
    {
        SCOPED_TRACE(entry->Identifier);
        json_t item{ { "identifier", entry->Identifier },
                     { "path", entry->Path },
                     { "sourceFileFnv1a64", HashFile(entry->Path) },
                     { "actions", json_t::array() } };
        try
        {
            LoadedObject loaded{ context->GetObjectRepository().LoadObject(entry, true) };
            if (!loaded.object)
                throw std::runtime_error("Object parse/image load failed");
            loaded.Load();
            const auto& peep = static_cast<const PeepAnimationsObject&>(*loaded.object);
            item["peepType"] = EnumValue(peep.GetPeepType());
            for (size_t group = 0; group < peep.GetNumAnimationGroups(); group++)
                for (const auto& [name, type] : getAnimationsByPeepType(peep.GetPeepType()))
                {
                    const auto groupId = static_cast<PeepAnimationGroup>(group);
                    const auto& animation = peep.GetPeepAnimation(groupId, type);
                    if (animation.frameOffsets.empty())
                        throw std::runtime_error("Required peep action has no frames");
                    json_t record{ { "sample",
                                     entry->Identifier + "_group" + std::to_string(group) + "_action"
                                         + std::to_string(EnumValue(type)) },
                                   { "group", group },
                                   { "action", name } };
                    auto expected = CompareRange(
                        *loaded.object, animation.baseImage,
                        *std::max_element(animation.frameOffsets.begin(), animation.frameOffsets.end()) + 1, record);
                    const auto inferred = inferMaxAnimationDimensions(animation);
                    EXPECT_EQ(
                        (std::array{ inferred.spriteWidth, inferred.spriteHeightNegative, inferred.spriteHeightPositive }),
                        expected);
                    if (groupId == PeepAnimationGroup::balloon || groupId == PeepAnimationGroup::hat
                        || groupId == PeepAnimationGroup::umbrella)
                        expected[1] = static_cast<uint8_t>(expected[1] + 12);
                    EXPECT_EQ(
                        (std::array{ animation.bounds.spriteWidth, animation.bounds.spriteHeightNegative,
                                     animation.bounds.spriteHeightPositive }),
                        expected);
                    record["loadedBoundsWithAccessoryAdjustment"] = expected;
                    item["actions"].push_back(record);
                    actions++;
                }
        }
        catch (const std::exception& error)
        {
            item["error"] = error.what();
            ADD_FAILURE() << error.what();
        }
        report["objects"].push_back(item);
    }
    report["objectCount"] = objects.size();
    report["actionCount"] = actions;
    EXPECT_GT(actions, 33u);
}
