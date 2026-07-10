/*****************************************************************************
 * Copyright (c) 2014-2026 OpenRCT2 developers
 *
 * For a complete list of all authors, please refer to contributors.md
 * Interested in contributing? Visit https://github.com/OpenRCT2/OpenRCT2
 *
 * OpenRCT2 is licensed under the GNU General Public License version 3.
 *****************************************************************************/

#pragma once

#include <cstdint>
#include <string_view>
#include <vector>

namespace OpenRCT2
{
    struct GameState_t;
    struct ObjectRepositoryItem;

    // Fork-owned save versions live in a high private band so future upstream OpenRCT2 versions can continue advancing
    // sequentially without colliding with this mod's format changes.
    constexpr uint16_t kOpenRCT2ModParkFileVersionBase = 60000;
    constexpr uint16_t kRidePriceTargetVersion = kOpenRCT2ModParkFileVersionBase;
    constexpr uint16_t kCentMoneyVersion = kOpenRCT2ModParkFileVersionBase + 1;
    constexpr uint16_t kParkEntranceFeeTargetVersion = kOpenRCT2ModParkFileVersionBase + 2;
    constexpr uint16_t kRideItemSalesHistoryVersion = kOpenRCT2ModParkFileVersionBase + 3;
    constexpr uint16_t kRideLengthScaleVersion = kOpenRCT2ModParkFileVersionBase + 4;
    constexpr uint16_t kRideRatingSamplesVersion = kOpenRCT2ModParkFileVersionBase + 5;
    constexpr uint16_t kMazeCapacityModeVersion = kOpenRCT2ModParkFileVersionBase + 6;
    constexpr uint16_t kRideRatingSampleScaleVersion = kOpenRCT2ModParkFileVersionBase + 7;
    constexpr uint16_t kRideStableStatsVersion = kOpenRCT2ModParkFileVersionBase + 8;
    constexpr uint16_t kRideRatingActiveSampleVectorVersion = kOpenRCT2ModParkFileVersionBase + 9;
    constexpr uint16_t kLongitudinalGStatsVersion = kOpenRCT2ModParkFileVersionBase + 10;
    constexpr uint16_t kRealisedLongitudinalGVersion = kOpenRCT2ModParkFileVersionBase + 11;
    constexpr uint16_t kTransportRideStatsVersion = kOpenRCT2ModParkFileVersionBase + 12;
    constexpr uint16_t kTransportJourneyRoutingVersion = kOpenRCT2ModParkFileVersionBase + 13;

    // Current version that is saved.
    constexpr uint32_t kParkFileCurrentVersion = kTransportJourneyRoutingVersion;

    // The minimum version that is forwards compatible with the current version.
    constexpr uint32_t kParkFileMinVersion = 57;

    // The minimum version that is backwards compatible with the current version.
    // If this is increased beyond 0, uncomment the checks in ParkFile.cpp and Context.cpp!
    constexpr uint32_t kParkFileMinSupportedVersion = 0x0;

    constexpr uint32_t kParkFileMagic = 0x4B524150; // PARK

    // ZStd compression levels to use for various types of saves
    constexpr int16_t kParkFileSaveCompressionLevel = 7;
    constexpr int16_t kParkFileAutoCompressionLevel = 4;
    constexpr int16_t kParkFileNetCompressionLevel = 4;

    struct IStream;

    // As uint16_t, in order to allow comparison with int32_t
    constexpr uint16_t kInversionsHolesShelteredEightsSplit = 6;
    constexpr uint16_t kLogFlumeSteepSlopeVersion = 16;
    constexpr uint16_t kBlockBrakeImprovementsVersion = 27;
    constexpr uint16_t kGigaCoasterInversions = 31;
    constexpr uint16_t kWoodenFlatToSteepVersion = 37;
    constexpr uint16_t k16BitParkHistoryVersion = 38;
    constexpr uint16_t kPeepNamesObjectsVersion = 39;
    constexpr uint16_t kWoodenRollerCoasterMediumLargeHalfLoopsVersion = 41;
    constexpr uint16_t kExtendedCorkscrewCoasterVersion = 42;
    constexpr uint16_t kExtendedTwisterCoasterVersion = 43;
    constexpr uint16_t kExtendedBoatHireVersion = 46;
    constexpr uint16_t kParkEntranceObjectLimitIncreased = 47;
    constexpr uint16_t kExtendedStandUpRollerCoasterVersion = 48;
    constexpr uint16_t kPeepAnimationObjectsVersion = 49;
    constexpr uint16_t kDiagonalLongFlatToSteepAndDiveLoopVersion = 50;
    constexpr uint16_t kEmbeddedParkPreviewChunk = 52;
    constexpr uint16_t kClimateObjectsVersion = 53;
    constexpr uint16_t kExtendedGoKartsVersion = 54;
    constexpr uint16_t kHigherInversionsHolesHelicesStatsVersion = 55;
    constexpr uint16_t kFixedObsoleteFootpathsVersion = 56;
    constexpr uint16_t kRevertToVanillaFairRidePriceCalculation = 58;
    constexpr uint16_t kParkFileVersionUprightQuarterHelices = 60;
    constexpr uint16_t kExtendedInvertedRollerCoasterVersion = 61;

    class ParkFileExporter
    {
    public:
        std::vector<const ObjectRepositoryItem*> ExportObjectsList;
        uint32_t TargetVersion = kParkFileCurrentVersion;

        void Export(GameState_t& gameState, std::string_view path, int16_t compressionLevel);
        void Export(GameState_t& gameState, IStream& stream, int16_t compressionLevel);
    };
} // namespace OpenRCT2
