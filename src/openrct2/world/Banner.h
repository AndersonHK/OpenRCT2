/*****************************************************************************
 * Copyright (c) 2014-2026 OpenRCT2 developers
 *
 * For a complete list of all authors, please refer to contributors.md
 * Interested in contributing? Visit https://github.com/OpenRCT2/OpenRCT2
 *
 * OpenRCT2 is licensed under the GNU General Public License version 3.
 *****************************************************************************/

#pragma once

#include "../Identifiers.h"
#include "../core/FlagHolder.hpp"
#include "../object/ObjectTypes.h"
#include "Location.hpp"

#include <string>

namespace OpenRCT2
{
    class Formatter;
    struct GameState_t;
    struct TileElement;
    struct WallElement;
} // namespace OpenRCT2

namespace OpenRCT2::Drawing
{
    enum class Colour : uint8_t;
    enum class TextColour : uint8_t;
} // namespace OpenRCT2::Drawing

constexpr OpenRCT2::ObjectEntryIndex kBannerNull = OpenRCT2::kObjectEntryIndexNull;
constexpr size_t kMaxBanners = 8192;

enum class BannerFlag : uint8_t
{
    noEntry = 0,
    isLargeScenery = 1,
    linkedToRide = 2,
    isWall = 3,
};
using BannerFlags = FlagHolder<uint8_t, BannerFlag>;

struct Banner
{
    BannerIndex id = BannerIndex::GetNull();
    OpenRCT2::Drawing::Colour colour{};
    TileCoordsXY position;

    Banner() = default;
    Banner(const Banner&) = default;
    Banner(Banner&&) noexcept = default;
    Banner& operator=(const Banner&);
    Banner& operator=(Banner&&) noexcept;

    OpenRCT2::ObjectEntryIndex getType() const noexcept
    {
        return _type;
    }
    BannerFlags getFlags() const noexcept
    {
        return _flags;
    }
    const std::string& getRawText() const noexcept
    {
        return _text;
    }
    RideId getRideIndex() const noexcept
    {
        return _rideIndex;
    }
    OpenRCT2::Drawing::TextColour getTextColour() const noexcept
    {
        return _textColour;
    }
    void setType(OpenRCT2::ObjectEntryIndex value);
    void setFlags(BannerFlags value);
    void setFlag(BannerFlag flag, bool value);
    void setText(std::string value);
    void setRideIndex(RideId value);
    void setTextColour(OpenRCT2::Drawing::TextColour value);

    bool isNull() const
    {
        return _type == kBannerNull;
    }

    std::string getTextWithColour() const;
    std::string getText() const;
    void formatTextWithColourTo(OpenRCT2::Formatter&) const;
    void formatTextTo(OpenRCT2::Formatter&) const;

private:
    OpenRCT2::ObjectEntryIndex _type = kBannerNull;
    BannerFlags _flags{};
    std::string _text;
    RideId _rideIndex{};
    OpenRCT2::Drawing::TextColour _textColour{};
};

void BannerInit(OpenRCT2::GameState_t& gameState);
OpenRCT2::TileElement* BannerGetTileElement(BannerIndex bannerIndex);
OpenRCT2::WallElement* BannerGetScrollingWallTileElement(BannerIndex bannerIndex);
RideId BannerGetClosestRideIndex(const CoordsXYZ& mapPos);
void BannerApplyFixes();
void UnlinkAllRideBanners();
void UnlinkAllBannersForRide(RideId rideId);
Banner* GetBanner(BannerIndex id);
Banner* GetOrCreateBanner(BannerIndex id);
Banner* CreateBanner();
void DeleteBanner(BannerIndex id);
void TrimBanners();
size_t GetNumBanners();
bool HasReachedBannerLimit();
