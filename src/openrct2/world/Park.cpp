/*****************************************************************************
 * Copyright (c) 2014-2026 OpenRCT2 developers
 *
 * For a complete list of all authors, please refer to contributors.md
 * Interested in contributing? Visit https://github.com/OpenRCT2/OpenRCT2
 *
 * OpenRCT2 is licensed under the GNU General Public License version 3.
 *****************************************************************************/

#include "Park.h"

#include "../Cheats.h"
#include "../Context.h"
#include "../Date.h"
#include "../Game.h"
#include "../GameState.h"
#include "../OpenRCT2.h"
#include "../actions/GameActionRunner.h"
#include "../actions/park/ParkSetParameterAction.h"
#include "../core/GameTime.hpp"
#include "../core/String.hpp"
#include "../entity/EntityList.h"
#include "../entity/Guest.h"
#include "../entity/Peep.h"
#include "../entity/Staff.h"
#include "../management/Award.h"
#include "../management/Finance.h"
#include "../management/Marketing.h"
#include "../management/Research.h"
#include "../network/Network.h"
#include "../profiling/Profiling.h"
#include "../ride/Ride.h"
#include "../ride/RideData.h"
#include "../ride/RideManager.hpp"
#include "../ride/ShopItem.h"
#include "../scenario/Scenario.h"
#include "../scripting/ScriptEngine.h"
#include "../ui/WindowManager.h"
#include "../util/Util.h"
#include "../windows/Intent.h"
#include "Entrance.h"
#include "Map.h"
#include "tile_element/EntranceElement.h"
#include "tile_element/SurfaceElement.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <type_traits>

using namespace OpenRCT2;
using namespace OpenRCT2::Scripting;

namespace OpenRCT2::Park
{
    static Guest* generateGuestFromCampaign(int32_t campaign);

    static bool IsSevenDayWeekStart(const Date& date)
    {
        constexpr int32_t kCalendarDaysPerOperatingYear = 245;

        auto monthsElapsed = static_cast<int32_t>(date.GetMonthsElapsed());
        auto elapsedDays = DateGetYear(monthsElapsed) * kCalendarDaysPerOperatingYear;
        for (int32_t month = 0; month < DateGetMonth(monthsElapsed); month++)
        {
            elapsedDays += Date::GetDaysInMonth(month);
        }
        elapsedDays += date.GetDay();

        return elapsedDays != 0 && (elapsedDays % GameTime::kDaysPerWeek) == 0;
    }

    static constexpr auto kParkEntranceValueNumerator = 7;
    static constexpr auto kParkEntranceValueDenominator = 10;

    /**
     * Choose a random peep spawn and iterates through until defined spawn is found.
     */
    static PeepSpawn* GetRandomPeepSpawn()
    {
        auto& gameState = getGameState();
        if (!gameState.peepSpawns.empty())
        {
            return &gameState.peepSpawns[ScenarioRand() % gameState.peepSpawns.size()];
        }

        return nullptr;
    }

    static money64 calculateRideValue(const Ride& ride)
    {
        money64 result = 0;
        if (ride.value != kRideValueUndefined)
        {
            const auto& rtd = ride.getRideTypeDescriptor();
            result = (ride.value * 10) * (static_cast<money64>(RideCustomersInLast5Minutes(ride)) + rtd.BonusValue * 4LL);
        }
        return result;
    }

    static money64 calculateTotalRideValueForMoney(const ParkData& park, const GameState_t& gameState)
    {
        money64 totalRideValue = 0;
        bool ridePricesUnlocked = RidePricesUnlocked(park) && !(gameState.park.flags & PARK_FLAGS_NO_MONEY);
        for (auto& ride : RideManager(gameState))
        {
            if (ride.status != RideStatus::open)
                continue;
            if (ride.flags.hasAny(RideFlag::brokenDown, RideFlag::crashed))
                continue;

            // Add ride value
            if (ride.value != kRideValueUndefined)
            {
                money64 rideValue = ride.value;
                if (ridePricesUnlocked)
                {
                    rideValue -= ride.price[0];
                }
                if (rideValue > 0)
                {
                    totalRideValue += rideValue * 2;
                }
            }
        }
        return totalRideValue;
    }

    static uint32_t calculateSuggestedMaxGuests(const ParkData& park, const GameState_t& gameState)
    {
        // Retained as a legacy UI/script estimate. Guest generation no longer uses this as a cap.
        uint32_t suggestedMaxGuests = 0;
        uint32_t difficultGenerationBonus = 0;

        for (auto& ride : RideManager(gameState))
        {
            if (ride.status != RideStatus::open)
                continue;
            if (ride.flags.hasAny(RideFlag::brokenDown, RideFlag::crashed))
                continue;

            // Add guest score for ride type
            suggestedMaxGuests += ride.getRideTypeDescriptor().BonusValue;

            // If difficult guest generation, extra guests are available for good rides
            if (park.flags & PARK_FLAGS_DIFFICULT_GUEST_GENERATION)
            {
                if (!ride.flags.has(RideFlag::tested))
                    continue;
                if (!ride.getRideTypeDescriptor().flags.has(RtdFlag::hasTrack))
                    continue;
                if (!ride.getRideTypeDescriptor().flags.has(RtdFlag::hasDataLogging))
                    continue;
                if (ride.getStation().SegmentLength < (600 << 16))
                    continue;
                if (ride.ratings.excitement < RideRating::make(6, 00))
                    continue;

                // Bonus guests for good ride
                difficultGenerationBonus += ride.getRideTypeDescriptor().BonusValue * 2;
            }
        }

        if (park.flags & PARK_FLAGS_DIFFICULT_GUEST_GENERATION)
        {
            suggestedMaxGuests = std::min<uint32_t>(suggestedMaxGuests, 1000);
            suggestedMaxGuests += difficultGenerationBonus;
        }

        suggestedMaxGuests = std::min<uint32_t>(suggestedMaxGuests, 65535);

#ifdef ENABLE_SCRIPTING
        auto& hookEngine = GetContext()->GetScriptEngine().GetHookEngine();
        if (hookEngine.HasSubscriptions(HookType::parkCalculateGuestCap))
        {
            JSContext* ctx = GetContext()->GetScriptEngine().GetContext();
            JSValue obj = JS_NewObject(ctx);
            JS_SetPropertyStr(ctx, obj, "suggestedGuestMaximum", JS_NewInt64(ctx, suggestedMaxGuests));
            hookEngine.Call(HookType::parkCalculateGuestCap, obj, true, true);

            suggestedMaxGuests = AsOrDefault(ctx, obj, "suggestedGuestMaximum", static_cast<int32_t>(suggestedMaxGuests));
            suggestedMaxGuests = std::clamp<uint16_t>(suggestedMaxGuests, 0, UINT16_MAX);

            JS_FreeValue(ctx, obj);
        }
#endif
        return suggestedMaxGuests;
    }

    static uint32_t QuantizeGuestGenerationProbability(double probability)
    {
        if (!std::isfinite(probability) || probability <= 0.0)
        {
            return 0;
        }

        if (probability < 1.0)
        {
            return 1;
        }

        constexpr double kMaxProbability = std::numeric_limits<uint16_t>::max();
        if (probability >= kMaxProbability)
        {
            return std::numeric_limits<uint16_t>::max();
        }

        return static_cast<uint32_t>(std::lround(probability));
    }

    uint32_t CalculateGuestGenerationProbability(const ParkData& park)
    {
        constexpr money64 kGuestGenerationBaselineParkValue = 40000.00_GBP;
        constexpr double kGuestGenerationRating700Probability = 850.0;

        // Rating now reflects average guest happiness, so crowding and queues affect arrivals through happiness.
        // Every 100 rating points doubles or halves generation around the 700-rating reference point.
        const auto clampedRating = std::clamp<uint16_t>(park.rating, 0, 999);
        double probability = kGuestGenerationRating700Probability
            * std::pow(2.0, (static_cast<double>(clampedRating) - 700.0) / 100.0);

        // Keep the tuned probability at $40,000 park value, then scale geometrically from park value.
        if (park.value <= 0)
        {
            probability = 0.0;
        }
        else
        {
            const auto valueScale = std::sqrt(
                static_cast<double>(park.value) / static_cast<double>(kGuestGenerationBaselineParkValue));
            probability *= valueScale;
        }

        if (park.flags & PARK_FLAGS_DIFFICULT_GUEST_GENERATION)
        {
            probability *= 0.75;
        }

        // Penalty for overpriced entrance fee relative to debuffed total ride value.
        auto entranceFee = GetEntranceFee(park);
        auto parkEntranceValue = (park.totalRideValueForMoney * kParkEntranceValueNumerator) / kParkEntranceValueDenominator;
        if (entranceFee > parkEntranceValue)
        {
            probability *= 0.25;
            // Extra penalty for very overpriced entrance fee
            if (entranceFee / 2 > parkEntranceValue)
            {
                probability *= 0.25;
            }
        }

        // Reward or penalties for park awards
        for (const auto& award : park.currentAwards)
        {
            // +/- 25% of the probability
            if (AwardIsPositive(award.type))
            {
                probability *= 1.25;
            }
            else
            {
                probability *= 0.75;
            }
        }

        return QuantizeGuestGenerationProbability(probability);
    }

    static void generateGuests(ParkData& park, GameState_t& gameState)
    {
        // Generate a new guest for some probability
        if (static_cast<int32_t>(ScenarioRand() & 0xFFFF) < park.guestGenerationProbability)
        {
            GenerateGuest();
        }

        // Extra guests generated by advertising campaigns
        for (const auto& campaign : park.marketingCampaigns)
        {
            // Random chance of guest generation
            auto probability = MarketingGetCampaignGuestGenerationProbability(campaign.type);
            auto random = ScenarioRandMax(std::numeric_limits<uint16_t>::max());
            if (random < probability)
            {
                generateGuestFromCampaign(campaign.type);
            }
        }
    }

    static Guest* generateGuestFromCampaign(int32_t campaign)
    {
        auto peep = GenerateGuest();
        if (peep != nullptr)
        {
            MarketingSetGuestCampaign(peep, campaign);
        }
        return peep;
    }

    template<typename T, size_t TSize>
    static void HistoryPushRecord(T history[TSize], T newItem)
    {
        for (size_t i = TSize - 1; i > 0; i--)
        {
            history[i] = history[i - 1];
        }
        history[0] = newItem;
    }

    void Initialise(ParkData& park, GameState_t& gameState)
    {
        park.name = LanguageGetString(STR_UNNAMED_PARK);
        gameState.pluginStorage = {};
        park.staffHandymanColour = Drawing::Colour::brightRed;
        park.staffMechanicColour = Drawing::Colour::lightBlue;
        park.staffSecurityColour = Drawing::Colour::yellow;
        park.numGuestsInPark = 0;
        park.numGuestsInParkLastWeek = 0;
        park.numGuestsHeadingForPark = 0;
        park.guestChangeModifier = 0;
        park.rating = 0;
        park.guestGenerationProbability = 0;
        park.totalRideValueForMoney = 0;
        park.suggestedGuestMaximum = 0;
        gameState.researchLastItem = std::nullopt;
        park.marketingCampaigns.clear();

        ResearchResetItems(gameState);
        FinanceInit();

        SetEveryRideTypeNotInvented();

        SetAllSceneryItemsInvented();

        park.entranceFee = 10.00_GBP;
        park.entranceFeeTarget = ParkEntranceFeeTarget::affordable;

        gameState.peepSpawns.clear();
        ParkEntranceReset();

        gameState.researchPriorities = EnumsToFlags(
            ResearchCategory::transport, ResearchCategory::gentle, ResearchCategory::rollercoaster, ResearchCategory::thrill,
            ResearchCategory::water, ResearchCategory::shop, ResearchCategory::sceneryGroup);
        gameState.researchFundingLevel = RESEARCH_FUNDING_NORMAL;

        gameState.scenarioOptions.guestInitialCash = 50.00_GBP;
        gameState.scenarioOptions.guestInitialHappiness = CalculateGuestInitialHappiness(50);
        gameState.scenarioOptions.guestInitialHunger = 200;
        gameState.scenarioOptions.guestInitialThirst = 200;
        gameState.scenarioOptions.objective.Type = Scenario::ObjectiveType::guestsBy;
        gameState.scenarioOptions.objective.Year = 4;
        gameState.scenarioOptions.objective.NumGuests = 1000;
        gameState.scenarioOptions.landPrice = 90.00_GBP;
        gameState.scenarioOptions.constructionRightsPrice = 40.00_GBP;
        park.flags = PARK_FLAGS_NO_MONEY | PARK_FLAGS_SHOW_REAL_GUEST_NAMES;
        UpdateEntranceFee(park);

        ResetHistories(park);
        FinanceResetHistory();
        AwardReset();

        gameState.scenarioOptions.name.clear();
        gameState.scenarioOptions.details = String::toStd(LanguageGetString(STR_NO_DETAILS_YET));
    }

    void Update(ParkData& park, GameState_t& gameState)
    {
        PROFILED_FUNCTION();

        // Every seven calendar days.
        if (gameState.date.IsDayStart() && IsSevenDayWeekStart(gameState.date))
        {
            UpdateHistories(park);
        }

        const auto currentTicks = gameState.currentTicks;
        auto* windowMgr = Ui::GetWindowManager();

        // Every ~13 seconds
        if (currentTicks % 512 == 0)
        {
            park.rating = CalculateParkRating(park, gameState);
            park.value = CalculateParkValue(park, gameState);
            park.companyValue = CalculateCompanyValue(park);
            park.totalRideValueForMoney = calculateTotalRideValueForMoney(park, gameState);
            UpdateEntranceFee(park);
            park.suggestedGuestMaximum = calculateSuggestedMaxGuests(park, gameState);
            park.guestGenerationProbability = CalculateGuestGenerationProbability(park);

            windowMgr->InvalidateByClass(WindowClass::finances);
            auto intent = Intent(INTENT_ACTION_UPDATE_PARK_RATING);
            ContextBroadcastIntent(&intent);
        }

        // Every ~102 seconds
        if (currentTicks % 4096 == 0)
        {
            UpdateSize(park);
        }

        generateGuests(park, gameState);
    }

    uint32_t CalculateParkSize(ParkData& park)
    {
        uint32_t tiles = 0;
        TileElementIterator it;
        TileElementIteratorBegin(&it);
        do
        {
            if (it.element->getType() == TileElementType::Surface)
            {
                if (it.element->asSurface()->GetOwnership() & (OWNERSHIP_CONSTRUCTION_RIGHTS_OWNED | OWNERSHIP_OWNED))
                {
                    tiles++;
                }
            }
        } while (TileElementIteratorNext(&it));

        return tiles;
    }

    int32_t CalculateParkRating(const ParkData& park, const GameState_t& gameState)
    {
        if (gameState.cheats.forcedParkRating != kForcedParkRatingDisabled)
        {
            return gameState.cheats.forcedParkRating;
        }

        uint64_t guestHappiness = 0;
        uint32_t guestCount = 0;
        for (auto peep : EntityList<Guest>())
        {
            if (!peep->outsideOfPark)
            {
                guestHappiness += (static_cast<uint32_t>(peep->happiness) + peep->happinessTarget) / 2;
                guestCount++;
            }
        }

        if (guestCount == 0)
        {
            return 500;
        }

        const auto averageHappiness = static_cast<int32_t>(guestHappiness / guestCount);
        return std::clamp((averageHappiness * 999 + (kPeepMaxHappiness / 2)) / kPeepMaxHappiness, 0, 999);
    }

    money64 CalculateParkValue(const ParkData& park, const GameState_t& gameState)
    {
        // Sum ride values
        money64 result = 0;
        for (const auto& ride : RideManager(gameState))
        {
            result += calculateRideValue(ride);
        }

        // +7.00 per guest
        result += static_cast<money64>(park.numGuestsInPark) * 7.00_GBP;

        return result;
    }

    money64 CalculateCompanyValue(const ParkData& park)
    {
        money64 result = park.value - park.bankLoan;

        result = AddClamp(result, park.cash);

        return result;
    }

    uint8_t CalculateGuestInitialHappiness(uint8_t percentage)
    {
        percentage = std::clamp<uint8_t>(percentage, 15, 98);

        // The percentages follow this sequence:
        //   15 17 18 20 21 23 25 26 28 29 31 32 34 36 37 39 40 42 43 45 47 48 50 51 53...
        // This sequence can be defined as PI*(9+n)/2 (the value is floored)
        for (uint8_t n = 1; n < 55; n++)
        {
            // Avoid floating point math by rescaling PI up.
            constexpr int32_t SCALE = 100000;
            constexpr int32_t PI_SCALED = 314159; // PI * SCALE;
            if (((PI_SCALED * (9 + n)) / SCALE) / 2 >= percentage)
            {
                return (9 + n) * 4;
            }
        }

        // This is the lowest possible value:
        return 40;
    }

    Guest* GenerateGuest()
    {
        Guest* peep = nullptr;
        const auto spawn = GetRandomPeepSpawn();
        if (spawn != nullptr)
        {
            auto direction = DirectionReverse(spawn->direction);
            peep = Guest::generate({ spawn->x, spawn->y, spawn->z });
            if (peep != nullptr)
            {
                peep->orientation = direction << 3;

                auto destination = peep->getLocation().ToTileCentre();
                peep->SetDestination(destination, 5);
                peep->PeepDirection = direction;
                peep->Var37 = 0;
                peep->State = PeepState::enteringPark;
            }
        }
        return peep;
    }

    void ResetHistories(ParkData& park)
    {
        std::fill(std::begin(park.ratingHistory), std::end(park.ratingHistory), kParkRatingHistoryUndefined);
        std::fill(std::begin(park.guestsInParkHistory), std::end(park.guestsInParkHistory), kGuestsInParkHistoryUndefined);
    }

    void UpdateHistories(ParkData& park)
    {
        uint8_t guestChangeModifier = 1;
        int32_t changeInGuestsInPark = static_cast<int32_t>(park.numGuestsInPark)
            - static_cast<int32_t>(park.numGuestsInParkLastWeek);
        if (changeInGuestsInPark > -20)
        {
            guestChangeModifier++;
            if (changeInGuestsInPark < 20)
            {
                guestChangeModifier = 0;
            }
        }
        park.guestChangeModifier = guestChangeModifier;
        park.numGuestsInParkLastWeek = park.numGuestsInPark;

        // Update park rating, guests in park and current cash history
        constexpr auto ratingHistorySize = std::extent_v<decltype(ParkData::ratingHistory)>;
        HistoryPushRecord<uint16_t, ratingHistorySize>(park.ratingHistory, park.rating);
        constexpr auto numGuestsHistorySize = std::extent_v<decltype(ParkData::guestsInParkHistory)>;
        HistoryPushRecord<uint32_t, numGuestsHistorySize>(park.guestsInParkHistory, park.numGuestsInPark);

        constexpr auto cashHistorySize = std::extent_v<decltype(ParkData::cashHistory)>;
        HistoryPushRecord<money64, cashHistorySize>(park.cashHistory, park.cash - park.bankLoan);

        // Update weekly profit history
        auto currentWeeklyProfit = park.weeklyProfitAverageDividend;
        if (park.weeklyProfitAverageDivisor != 0)
        {
            currentWeeklyProfit /= park.weeklyProfitAverageDivisor;
        }
        constexpr auto profitHistorySize = std::extent_v<decltype(ParkData::weeklyProfitHistory)>;
        HistoryPushRecord<money64, profitHistorySize>(park.weeklyProfitHistory, currentWeeklyProfit);
        park.weeklyProfitAverageDividend = 0;
        park.weeklyProfitAverageDivisor = 0;

        // Update park value history
        constexpr auto parkValueHistorySize = std::extent_v<decltype(ParkData::weeklyProfitHistory)>;
        HistoryPushRecord<money64, parkValueHistorySize>(park.valueHistory, park.value);

        // Invalidate relevant windows
        auto intent = Intent(INTENT_ACTION_UPDATE_GUEST_COUNT);
        ContextBroadcastIntent(&intent);

        auto* windowMgr = Ui::GetWindowManager();
        windowMgr->InvalidateByClass(WindowClass::parkInformation);
        windowMgr->InvalidateByClass(WindowClass::finances);
    }

    uint32_t UpdateSize(ParkData& park)
    {
        auto tiles = CalculateParkSize(park);
        if (tiles != park.size)
        {
            park.size = tiles;

            auto* windowMgr = Ui::GetWindowManager();
            windowMgr->InvalidateByClass(WindowClass::parkInformation);
        }
        return tiles;
    }

    void SetOpen(const ParkData& park, bool open)
    {
        auto parkSetParameter = GameActions::ParkSetParameterAction(
            open ? GameActions::ParkParameter::open : GameActions::ParkParameter::close);
        GameActions::Execute(&parkSetParameter, getGameState());
    }

    /**
     *
     *  rct2: 0x00664D05
     */
    void UpdateFences(const CoordsXY& coords)
    {
        if (MapIsEdge(coords))
            return;

        auto surfaceElement = MapGetSurfaceElementAt(coords);
        if (surfaceElement == nullptr)
            return;

        uint8_t newFences = 0;
        if ((surfaceElement->GetOwnership() & OWNERSHIP_OWNED) == 0)
        {
            bool fenceRequired = true;

            TileElement* tileElement = MapGetFirstElementAt(coords);
            if (tileElement == nullptr)
                return;
            // If an entrance element do not place flags around surface
            do
            {
                if (tileElement->getType() != TileElementType::Entrance)
                    continue;

                if (tileElement->asEntrance()->GetEntranceType() != ENTRANCE_TYPE_PARK_ENTRANCE)
                    continue;

                if (!(tileElement->isGhost()))
                {
                    fenceRequired = false;
                    break;
                }
            } while (!(tileElement++)->isLastForTile());

            if (fenceRequired)
            {
                if (MapIsLocationInPark({ coords.x - kCoordsXYStep, coords.y }))
                {
                    newFences |= 0x8;
                }

                if (MapIsLocationInPark({ coords.x, coords.y - kCoordsXYStep }))
                {
                    newFences |= 0x4;
                }

                if (MapIsLocationInPark({ coords.x + kCoordsXYStep, coords.y }))
                {
                    newFences |= 0x2;
                }

                if (MapIsLocationInPark({ coords.x, coords.y + kCoordsXYStep }))
                {
                    newFences |= 0x1;
                }
            }
        }

        if (surfaceElement->GetParkFences() != newFences)
        {
            int32_t baseZ = surfaceElement->getBaseZ();
            int32_t clearZ = baseZ + 16;
            MapInvalidateTile({ coords, baseZ, clearZ });
            surfaceElement->SetParkFences(newFences);
        }
    }

    void UpdateFencesAroundTile(const CoordsXY& coords)
    {
        UpdateFences(coords);
        UpdateFences({ coords.x + kCoordsXYStep, coords.y });
        UpdateFences({ coords.x - kCoordsXYStep, coords.y });
        UpdateFences({ coords.x, coords.y + kCoordsXYStep });
        UpdateFences({ coords.x, coords.y - kCoordsXYStep });
    }

    void SetForcedRating(ParkData& park, int32_t rating)
    {
        auto& gameState = getGameState();
        gameState.cheats.forcedParkRating = rating;

        park.rating = CalculateParkRating(park, gameState);

        auto intent = Intent(INTENT_ACTION_UPDATE_PARK_RATING);
        ContextBroadcastIntent(&intent);
    }

    int32_t GetForcedRating()
    {
        return getGameState().cheats.forcedParkRating;
    }

    static money64 ClampEntranceFee(money64 entranceFee)
    {
        return std::clamp(entranceFee, 0.00_GBP, kMaxEntranceFee);
    }

    static std::array<money64, 4> GetGuestSpawnCashSamples(const ParkData& park)
    {
        auto& gameState = getGameState();
        if ((park.flags & PARK_FLAGS_NO_MONEY) || gameState.scenarioOptions.guestInitialCash == kMoney64Undefined)
        {
            return { 0.00_GBP, 0.00_GBP, 0.00_GBP, 0.00_GBP };
        }

        if (gameState.scenarioOptions.guestInitialCash == 0.00_GBP)
        {
            return { 50.00_GBP, 50.00_GBP, 50.00_GBP, 50.00_GBP };
        }

        const auto initialCash = gameState.scenarioOptions.guestInitialCash;
        return {
            std::max(0.00_GBP, initialCash - 10.00_GBP),
            std::max(0.00_GBP, initialCash),
            std::max(0.00_GBP, initialCash + 10.00_GBP),
            std::max(0.00_GBP, initialCash + 20.00_GBP),
        };
    }

    static money64 GetDebuffedParkEntranceValue(const ParkData& park)
    {
        if (park.totalRideValueForMoney <= 0)
        {
            return 0.00_GBP;
        }
        return (park.totalRideValueForMoney * kParkEntranceValueNumerator) / kParkEntranceValueDenominator;
    }

    static money64 GetProfitMaximisingEntranceFee(const ParkData& park)
    {
        const auto parkEntranceValue = GetDebuffedParkEntranceValue(park);
        const auto maximumFee = ClampEntranceFee(parkEntranceValue);
        if (maximumFee == 0.00_GBP)
        {
            return 0.00_GBP;
        }

        auto cashSamples = GetGuestSpawnCashSamples(park);
        std::sort(cashSamples.begin(), cashSamples.end());

        std::array<money64, 5> candidates{};
        size_t candidateCount = 0;
        candidates[candidateCount++] = maximumFee;
        for (auto cash : cashSamples)
        {
            candidates[candidateCount++] = std::min(cash, maximumFee);
        }
        std::sort(candidates.begin(), candidates.begin() + candidateCount);
        candidateCount = static_cast<size_t>(
            std::unique(candidates.begin(), candidates.begin() + candidateCount) - candidates.begin());

        money64 bestPrice = 0.00_GBP;
        money64 bestRevenue = 0.00_GBP;
        for (size_t i = 0; i < candidateCount; i++)
        {
            const auto candidate = candidates[i];
            const auto affordableGuests = static_cast<money64>(
                std::count_if(cashSamples.begin(), cashSamples.end(), [candidate](money64 cash) { return cash >= candidate; }));
            const auto revenue = candidate * affordableGuests;
            if (revenue > bestRevenue)
            {
                bestPrice = candidate;
                bestRevenue = revenue;
            }
        }

        return bestPrice;
    }

    money64 GetEntranceFeeForTarget(const ParkData& park, ParkEntranceFeeTarget target)
    {
        if (target == ParkEntranceFeeTarget::custom)
        {
            return ClampEntranceFee(park.entranceFee);
        }

        const auto parkEntranceValue = GetDebuffedParkEntranceValue(park);
        const auto cashSamples = GetGuestSpawnCashSamples(park);

        switch (target)
        {
            case ParkEntranceFeeTarget::incomePerGuest:
                return ClampEntranceFee(std::min(*std::max_element(cashSamples.begin(), cashSamples.end()), parkEntranceValue));
            case ParkEntranceFeeTarget::profit:
                return GetProfitMaximisingEntranceFee(park);
            case ParkEntranceFeeTarget::affordable:
                return ClampEntranceFee(std::min(*std::min_element(cashSamples.begin(), cashSamples.end()), parkEntranceValue));
            case ParkEntranceFeeTarget::custom:
                break;
        }

        return 0.00_GBP;
    }

    void UpdateEntranceFee(ParkData& park)
    {
        if (park.entranceFeeTarget != ParkEntranceFeeTarget::custom)
        {
            park.entranceFee = GetEntranceFeeForTarget(park, park.entranceFeeTarget);
        }
    }

    money64 GetEntranceFee(const ParkData& park)
    {
        if (park.flags & PARK_FLAGS_NO_MONEY)
        {
            return 0;
        }
        if (!EntranceFeeUnlocked(park))
        {
            return 0;
        }

        return GetEntranceFeeForTarget(park, park.entranceFeeTarget);
    }

    bool RidePricesUnlocked(const ParkData& park)
    {
        if (park.flags & PARK_FLAGS_UNLOCK_ALL_PRICES)
        {
            return true;
        }
        if (park.flags & PARK_FLAGS_PARK_FREE_ENTRY)
        {
            return true;
        }
        return false;
    }

    bool EntranceFeeUnlocked(const ParkData& park)
    {
        if (park.flags & PARK_FLAGS_UNLOCK_ALL_PRICES)
        {
            return true;
        }
        if (!(park.flags & PARK_FLAGS_PARK_FREE_ENTRY))
        {
            return true;
        }
        return false;
    }

    bool IsOpen(const ParkData& park)
    {
        return (park.flags & PARK_FLAGS_PARK_OPEN) != 0;
    }
} // namespace OpenRCT2::Park
