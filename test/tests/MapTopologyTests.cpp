/*****************************************************************************
 * Copyright (c) 2014-2026 OpenRCT2 developers
 *
 * For a complete list of all authors, please refer to contributors.md
 * Interested in contributing? Visit https://github.com/OpenRCT2/OpenRCT2
 *
 * OpenRCT2 is licensed under the GNU General Public License version 3.
 *****************************************************************************/

#include <gtest/gtest.h>
#include <openrct2/world/MapTopology.h>

using namespace OpenRCT2;

TEST(MapTopologyTest, ResetAdvancesAllChunkGenerationsTogether)
{
    const auto oldEpoch = MapTopology::GetEpoch();

    MapTopology::Reset();

    const auto newEpoch = MapTopology::GetEpoch();
    EXPECT_GT(newEpoch, oldEpoch);
    EXPECT_EQ(MapTopology::GetChunkGeneration(TileCoordsXY{ 0, 0 }), newEpoch);
    EXPECT_EQ(MapTopology::GetChunkGeneration(TileCoordsXY{ kMaximumMapSizeTechnical - 1, 0 }), newEpoch);
    EXPECT_EQ(
        MapTopology::GetChunkGeneration(TileCoordsXY{ kMaximumMapSizeTechnical - 1, kMaximumMapSizeTechnical - 1 }), newEpoch);
}

TEST(MapTopologyTest, InteriorEditOnlyAdvancesItsOwnChunk)
{
    MapTopology::Reset();
    const auto oldEpoch = MapTopology::GetEpoch();
    const auto untouchedGeneration = MapTopology::GetChunkGeneration(TileCoordsXY{ 32, 32 });

    MapTopology::InvalidateTileAndNeighbours(TileCoordsXY{ 1, 1 });

    const auto newEpoch = MapTopology::GetEpoch();
    EXPECT_GT(newEpoch, oldEpoch);
    EXPECT_EQ(MapTopology::GetChunkGeneration(TileCoordsXY{ 1, 1 }), newEpoch);
    EXPECT_EQ(MapTopology::GetChunkGeneration(TileCoordsXY{ 32, 32 }), untouchedGeneration);
}

TEST(MapTopologyTest, ChunkBoundaryEditAdvancesOnlyCardinallyAdjacentChunks)
{
    MapTopology::Reset();
    const auto untouchedGeneration = MapTopology::GetChunkGeneration(TileCoordsXY{ 32, 16 });

    MapTopology::InvalidateTileAndNeighbours(TileCoordsXY{ 16, 16 });

    const auto newEpoch = MapTopology::GetEpoch();
    EXPECT_EQ(MapTopology::GetChunkGeneration(TileCoordsXY{ 16, 16 }), newEpoch);
    EXPECT_EQ(MapTopology::GetChunkGeneration(TileCoordsXY{ 15, 16 }), newEpoch);
    EXPECT_EQ(MapTopology::GetChunkGeneration(TileCoordsXY{ 16, 15 }), newEpoch);
    EXPECT_EQ(MapTopology::GetChunkGeneration(TileCoordsXY{ 32, 16 }), untouchedGeneration);
    EXPECT_EQ(MapTopology::GetChunkGeneration(TileCoordsXY{ 16, 32 }), untouchedGeneration);
    EXPECT_EQ(MapTopology::GetChunkGeneration(TileCoordsXY{ 15, 15 }), untouchedGeneration);
}

TEST(MapTopologyTest, UpperChunkEdgesAdvanceRightAndLowerNeighbours)
{
    MapTopology::Reset();
    const auto untouchedGeneration = MapTopology::GetChunkGeneration(TileCoordsXY{ 0, 0 });

    MapTopology::InvalidateTileAndNeighbours(TileCoordsXY{ 15, 15 });

    const auto newEpoch = MapTopology::GetEpoch();
    EXPECT_EQ(MapTopology::GetChunkGeneration(TileCoordsXY{ 15, 15 }), newEpoch);
    EXPECT_EQ(MapTopology::GetChunkGeneration(TileCoordsXY{ 16, 15 }), newEpoch);
    EXPECT_EQ(MapTopology::GetChunkGeneration(TileCoordsXY{ 15, 16 }), newEpoch);
    EXPECT_EQ(MapTopology::GetChunkGeneration(TileCoordsXY{ 16, 16 }), untouchedGeneration);
}

TEST(MapTopologyTest, InvalidTileDoesNotAdvanceTheEpoch)
{
    MapTopology::Reset();
    const auto epoch = MapTopology::GetEpoch();

    MapTopology::InvalidateTileAndNeighbours(TileCoordsXY{ -1, 0 });
    MapTopology::InvalidateTileAndNeighbours(CoordsXY{ -1, 0 });

    EXPECT_EQ(MapTopology::GetEpoch(), epoch);
    EXPECT_EQ(MapTopology::GetChunkGeneration(TileCoordsXY{ -1, 0 }), epoch);
    EXPECT_EQ(MapTopology::GetChunkGeneration(CoordsXY{ -1, 0 }), epoch);
}
