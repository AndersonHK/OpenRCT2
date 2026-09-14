/*****************************************************************************
 * Copyright (c) 2014-2026 OpenRCT2 developers
 *
 * For a complete list of all authors, please refer to contributors.md
 * Interested in contributing? Visit https://github.com/OpenRCT2/OpenRCT2
 *
 * OpenRCT2 is licensed under the GNU General Public License version 3.
 *****************************************************************************/

#pragma once

#include "../core/EnumUtils.hpp"
#include "../core/FlagHolder.hpp"
#include "../management/Award.h"
#include "../management/Finance.h"
#include "../management/Marketing.h"
#include "Location.hpp"

#include <string>
#include <vector>

namespace OpenRCT2::Drawing
{
    enum class Colour : uint8_t;
}

constexpr auto kMaxEntranceFee = 999.00_GBP;

constexpr uint16_t kParkRatingHistoryUndefined = std::numeric_limits<uint16_t>::max();
constexpr uint32_t kGuestsInParkHistoryUndefined = std::numeric_limits<uint32_t>::max();
constexpr uint8_t kParkRatingHistorySize = 32;
constexpr uint8_t kGuestsInParkHistorySize = 32;
constexpr uint8_t kParkNameMaxLength = 128;
constexpr uint8_t kScenarioNameMaxLength = 128;
constexpr uint16_t kScenarioDetailsNameMaxLength = 256;

enum class ParkFlag : uint32_t
{
    parkOpen = 0,
    scenarioCompleteNameInput = 1,
    forbidLandscapeChanges = 2,
    forbidTreeRemoval = 3,
    showRealGuestNames = 4,
    forbidHighConstruction = 5, // below tree height
    guestPreferLessIntenseRides = 6,
    forbidMarketingCampaigns = 7,
    antiCheatDeprecated = 8, // Not used anymore, used for cheat detection
    guestPreferMoreIntenseRides = 9,
    showRealStaffNames = 10,
    noMoney = 11,
    difficultGuestGeneration = 12,
    freeEntry = 13,
    difficultParkRating = 14,
    lockRealNamesOptionDeprecated = 15, // Deprecated now we use a persistent 'real names' setting
    noMoneyScenario = 17,               // Deprecated, originally used in scenario editor
    spritesInitialised = 18,            // After a scenario is loaded this prevents edits in the scenario editor
    sixFlagsDeprecated = 19,            // Not used anymore

    rct1Interest = 30,    // OpenRCT2 only
    unlockAllPrices = 31, // OpenRCT2 only
};
using ParkFlags = FlagHolder<uint64_t, ParkFlag>;

namespace OpenRCT2::Park
{
    enum class ParkEntranceFeeTarget : uint8_t
    {
        incomePerGuest,
        profit,
        affordable,
        custom,
    };

    struct ParkData final
    {
        std::string name;
        ParkFlags flags;
        uint16_t rating;
        uint16_t ratingHistory[kParkRatingHistorySize];
        int16_t ratingCasualtyPenalty;
        money64 entranceFee;
        ParkEntranceFeeTarget entranceFeeTarget{ ParkEntranceFeeTarget::affordable };
        std::vector<CoordsXYZD> entrances;
        uint32_t size;
        money64 value;
        money64 valueHistory[kFinanceHistorySize];

        money64 cash;
        money64 cashHistory[kFinanceHistorySize];

        uint16_t weeklyProfitAverageDivisor;
        money64 weeklyProfitAverageDividend;
        money64 weeklyProfitHistory[kFinanceHistorySize];

        // The total profit for the entire scenario that precedes the current financial table.
        money64 historicalProfit;
        money64 currentProfit;
        money64 expenditureTable[kExpenditureTableMonthCount][EnumValue(ExpenditureType::count)];
        money64 currentExpenditure;

        money64 companyValue;

        uint32_t guestsInParkHistory[kGuestsInParkHistorySize];
        uint32_t numGuestsInPark;
        uint32_t numGuestsHeadingForPark;
        uint32_t numGuestsInParkLastWeek;
        uint8_t guestChangeModifier;

        /**
         * Probability out of 65535, of gaining a new guest per game tick.
         * new guests per second = 40 * (probability / 65535)
         * This is driven by an exponential park-rating curve and scaled geometrically from park value, using $50,000 as
         * the baseline. Guest count does not directly cap generation; crowded paths and long queues reduce arrivals by
         * reducing happiness.
         */
        int32_t guestGenerationProbability;

        /**
         * Legacy UI/script estimate of how many guests the current ride mix can support.
         * Guest generation no longer treats this value as a cap.
         */
        uint32_t suggestedGuestMaximum;

        uint64_t totalAdmissions;
        money64 totalIncomeFromAdmissions;

        money64 totalRideValueForMoney;

        money64 bankLoan;
        money64 maxBankLoan;
        uint8_t bankLoanInterestRate;

        uint64_t samePriceThroughoutPark{};

        Drawing::Colour staffHandymanColour;
        Drawing::Colour staffMechanicColour;
        Drawing::Colour staffSecurityColour;

        uint8_t peepWarningThrottle[16];

        std::vector<Award> currentAwards;
        std::vector<MarketingCampaign> marketingCampaigns;
    };
} // namespace OpenRCT2::Park
