/*****************************************************************************
 * Copyright (c) 2014-2026 OpenRCT2 developers
 *
 * For a complete list of all authors, please refer to contributors.md
 * Interested in contributing? Visit https://github.com/OpenRCT2/OpenRCT2
 *
 * OpenRCT2 is licensed under the GNU General Public License version 3.
 *****************************************************************************/

#include <gtest/gtest.h>
#include <openrct2/Context.h>
#include <openrct2/GameState.h>
#include <openrct2/OpenRCT2.h>
#include <openrct2/actions/terraform/ClearAction.h>
#include <openrct2/core/DataSerialiser.h>
#include <openrct2/world/Map.h>
#include <openrct2/world/TileElementsView.h>
#include <openrct2/world/tile_element/PathElement.h>
#include <openrct2/world/tile_element/TileElement.h>
#include <openrct2/world/tile_element/WallElement.h>

using namespace OpenRCT2;
using namespace OpenRCT2::GameActions;

TEST(ClearScenery, EveryMaskKeepsWallAndPathAdditionSelectionsIndependentAfterSerialisation)
{
    const auto previousScene = gLegacyScene;
    gOpenRCT2Headless = true;
    gOpenRCT2NoGraphics = true;
    auto context = CreateContext();
    ASSERT_TRUE(context->Initialise());
    MapInit({ 16, 16 });
    gLegacyScene = LegacyScene::scenarioEditor;
    auto& state = getGameState();
    constexpr TileCoordsXY tile{ 3, 3 };
    constexpr CoordsXY coords{ 96, 96 };

    // Two real walls exercise iterator restart after erasure; ghost wall/path must survive.
    for (uint8_t mask = 0; mask < 32; ++mask)
    {
        SCOPED_TRACE(static_cast<int>(mask));
        std::vector<TileElement> elements;
        elements.push_back(*MapGetFirstElementAt(coords));
        elements.back().setLastForTile(false);
        for (int height : { 4, 8 })
        {
            TileElement wall{};
            wall.clearAs(TileElementType::wall);
            wall.baseHeight = height;
            wall.clearanceHeight = height + 2;
            wall.asWall()->SetBannerIndex(BannerIndex::GetNull());
            elements.push_back(wall);
        }
        TileElement path{};
        path.clearAs(TileElementType::path);
        path.baseHeight = 12;
        path.clearanceHeight = 14;
        path.asPath()->SetAddition(1);
        elements.push_back(path);
        auto ghostWall = elements[1];
        ghostWall.setGhost(true);
        ghostWall.baseHeight = 16;
        ghostWall.clearanceHeight = 18;
        elements.push_back(ghostWall);
        auto ghostPath = path;
        ghostPath.setGhost(true);
        ghostPath.baseHeight = 20;
        ghostPath.clearanceHeight = 22;
        ghostPath.setLastForTile(true);
        elements.push_back(ghostPath);
        ASSERT_EQ(ReplaceTileElementsAt(tile, std::move(elements)), TileMutationStatus::ok);

        ClearAction original(MapRange{ coords, coords }, ClearableItems{ mask });
        DataSerialiser writer(true);
        original.Serialise(writer);
        writer.GetStream().SetPosition(0);
        DataSerialiser reader(false, writer.GetStream());
        ClearAction action;
        action.Serialise(reader);

        auto count = [&](TileElementType type, bool ghost) {
            size_t result = 0;
            for (auto* element : TileElementsView<TileElement>(tile))
                if (element->getType() == type && element->isGhost() == ghost)
                    ++result;
            return result;
        };
        const auto query = action.Query(state, state.park);
        ASSERT_EQ(query.error, Status::ok);
        EXPECT_EQ(count(TileElementType::wall, false), 2u);
        EXPECT_EQ(count(TileElementType::path, false), 1u);
        ASSERT_NE(MapGetFootpathElement({ coords, 96 }), nullptr);
        EXPECT_TRUE(MapGetFootpathElement({ coords, 96 })->asPath()->HasAddition());

        const auto result = action.Execute(state, state.park);
        EXPECT_EQ(result.error, Status::ok);
        EXPECT_EQ(result.cost, query.cost);
        EXPECT_EQ(count(TileElementType::wall, false), (mask & 8) ? 0u : 2u);
        EXPECT_EQ(count(TileElementType::path, false), (mask & 4) ? 0u : 1u);
        if (!(mask & 4))
        {
            auto* remainingPath = MapGetFootpathElement({ coords, 96 });
            ASSERT_NE(remainingPath, nullptr);
            EXPECT_EQ(remainingPath->asPath()->HasAddition(), !(mask & 16));
        }
        EXPECT_EQ(count(TileElementType::wall, true), 1u);
        EXPECT_EQ(count(TileElementType::path, true), 1u);
        auto* remainingGhost = MapGetFootpathElement({ coords, 160 });
        ASSERT_NE(remainingGhost, nullptr);
        EXPECT_TRUE(remainingGhost->asPath()->HasAddition());
    }
    gLegacyScene = previousScene;
}
