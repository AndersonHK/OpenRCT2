/*****************************************************************************
 * Copyright (c) 2014-2026 OpenRCT2 developers
 *
 * For a complete list of all authors, please refer to contributors.md
 * Interested in contributing? Visit https://github.com/OpenRCT2/OpenRCT2
 *
 * OpenRCT2 is licensed under the GNU General Public License version 3.
 *****************************************************************************/

#include "ParkMarketingAction.h"

#include "../../core/GameTime.hpp"
#include "../../localisation/StringIds.h"
#include "../../management/Finance.h"
#include "../../management/Marketing.h"
#include "../../ride/Ride.h"
#include "../../ui/WindowManager.h"
#include "../../windows/Intent.h"
#include "../../world/ParkData.h"

#include <algorithm>
#include <iterator>
#include <limits>

namespace OpenRCT2::GameActions
{
    ParkMarketingAction::ParkMarketingAction(int32_t type, int32_t item, int32_t numWeeks)
        : _type(type)
        , _item(item)
        , _numWeeks(numWeeks)
    {
    }

    void ParkMarketingAction::AcceptParameters(GameActionParameterVisitor& visitor)
    {
        visitor.Visit("type", _type);
        visitor.Visit("item", _item);
        visitor.Visit("duration", _numWeeks);
    }

    uint16_t ParkMarketingAction::GetActionFlags() const
    {
        return GameAction::GetActionFlags() | Flags::AllowWhilePaused;
    }

    void ParkMarketingAction::Serialise(DataSerialiser& stream)
    {
        GameAction::Serialise(stream);
        stream << DS_TAG(_type) << DS_TAG(_item) << DS_TAG(_numWeeks);
    }

    Result ParkMarketingAction::Query(GameState_t& gameState, Park::ParkData& park) const
    {
        if (static_cast<size_t>(_type) >= std::size(AdvertisingCampaignPricePerWeek) || _numWeeks >= 256)
        {
            return Result(Status::invalidParameters, STR_CANT_START_MARKETING_CAMPAIGN, STR_ERR_VALUE_OUT_OF_RANGE);
        }
        if (park.flags & PARK_FLAGS_FORBID_MARKETING_CAMPAIGN)
        {
            return Result(
                Status::disallowed, STR_CANT_START_MARKETING_CAMPAIGN, STR_MARKETING_CAMPAIGNS_FORBIDDEN_BY_LOCAL_AUTHORITY);
        }
        if (_type == ADVERTISING_CAMPAIGN_RIDE_FREE || _type == ADVERTISING_CAMPAIGN_RIDE)
        {
            if (_item < 0 || static_cast<uint64_t>(_item) > std::numeric_limits<RideId::UnderlyingType>::max())
            {
                return Result(Status::invalidParameters, STR_CANT_START_MARKETING_CAMPAIGN, STR_ERR_RIDE_NOT_FOUND);
            }
            const auto* ride = GetRide(RideId::FromUnderlying(static_cast<RideId::UnderlyingType>(_item)));
            if (ride == nullptr)
            {
                return Result(Status::invalidParameters, STR_CANT_START_MARKETING_CAMPAIGN, STR_ERR_RIDE_NOT_FOUND);
            }
            if (!MarketingIsRideCampaignEligible(*ride))
            {
                return Result(Status::invalidParameters, STR_CANT_START_MARKETING_CAMPAIGN, STR_INVALID_RIDE_TYPE);
            }
        }

        return CreateResult();
    }

    Result ParkMarketingAction::Execute(GameState_t& gameState, Park::ParkData& park) const
    {
        MarketingCampaign campaign{};
        campaign.type = _type;
        campaign.weeksLeft = static_cast<uint8_t>(
            std::min<int32_t>(_numWeeks * OpenRCT2::GameTime::kDaysPerWeek, std::numeric_limits<uint8_t>::max()));
        if (campaign.type == ADVERTISING_CAMPAIGN_RIDE_FREE || campaign.type == ADVERTISING_CAMPAIGN_RIDE)
        {
            campaign.rideId = RideId::FromUnderlying(_item);
        }
        else if (campaign.type == ADVERTISING_CAMPAIGN_FOOD_OR_DRINK_FREE)
        {
            campaign.shopItemType = ShopItem(_item);
        }
        MarketingNewCampaign(campaign);

        // We are only interested in invalidating the finances (marketing) window
        auto windowManager = Ui::GetWindowManager();
        windowManager->BroadcastIntent(Intent(INTENT_ACTION_UPDATE_CASH));

        return CreateResult();
    }

    Result ParkMarketingAction::CreateResult() const
    {
        auto result = Result();
        result.errorTitle = STR_CANT_START_MARKETING_CAMPAIGN;
        result.expenditure = ExpenditureType::marketing;
        result.cost = CalculatePrice();
        return result;
    }

    money64 ParkMarketingAction::CalculatePrice() const
    {
        return _numWeeks * AdvertisingCampaignPricePerWeek[_type];
    }
} // namespace OpenRCT2::GameActions
