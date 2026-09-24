/*****************************************************************************
 * Copyright (c) 2014-2026 OpenRCT2 developers
 *
 * For a complete list of all authors, please refer to contributors.md
 * Interested in contributing? Visit https://github.com/OpenRCT2/OpenRCT2
 *
 * OpenRCT2 is licensed under the GNU General Public License version 3.
 *****************************************************************************/

#include "Banner.h"

#include "../Diagnostic.h"
#include "../GameState.h"
#include "../OpenRCT2.h"
#include "../drawing/PaletteIndex.h"
#include "../drawing/ScrollingText.h"
#include "../localisation/Formatter.h"
#include "../localisation/Formatting.h"
#include "../localisation/Language.h"
#include "../object/WallSceneryEntry.h"
#include "../ride/Ride.h"
#include "../ride/RideData.h"
#include "../ride/RideManager.hpp"
#include "Map.h"
#include "TileElementsView.h"
#include "WorldBannerPresentation.h"
#include "WorldObjectPresentation.h"
#include "tile_element/BannerElement.h"
#include "tile_element/TileElement.h"
#include "tile_element/TrackElement.h"
#include "tile_element/WallElement.h"

#include <bitset>
#include <limits>

using namespace OpenRCT2;

namespace
{
    std::bitset<kMaxBanners> _dirtyBannerText;
    std::array<uint16_t, kMaxBanners> _dirtyBannerIds{};
    size_t _dirtyBannerCount{};
    uint64_t _bannerTextEpoch{ 1 };

    void MarkBannerTextDirty(BannerIndex id) noexcept
    {
        const auto index = id.ToUnderlying();
        if (index < kMaxBanners && !_dirtyBannerText.test(index))
        {
            _dirtyBannerText.set(index);
            _dirtyBannerIds[_dirtyBannerCount++] = index;
        }
    }
} // namespace

Banner& Banner::operator=(const Banner& other)
{
    if (this != &other)
    {
        Banner copy(other);
        *this = std::move(copy);
    }
    return *this;
}

Banner& Banner::operator=(Banner&& other) noexcept
{
    if (this != &other)
    {
        MarkBannerTextDirty(id);
        id = other.id;
        _type = other._type;
        _flags = other._flags;
        _text = std::move(other._text);
        colour = other.colour;
        _rideIndex = other._rideIndex;
        _textColour = other._textColour;
        position = other.position;
        MarkBannerTextDirty(id);
    }
    return *this;
}

void Banner::setType(ObjectEntryIndex value)
{
    if (_type != value)
    {
        _type = value;
        MarkBannerTextDirty(id);
    }
}
void Banner::setFlags(BannerFlags value)
{
    if (_flags != value)
    {
        _flags = value;
        MarkBannerTextDirty(id);
    }
}
void Banner::setFlag(BannerFlag flag, bool value)
{
    auto flags = _flags;
    flags.set(flag, value);
    setFlags(flags);
}
void Banner::setText(std::string value)
{
    if (_text != value)
    {
        _text = std::move(value);
        MarkBannerTextDirty(id);
    }
}
void Banner::setRideIndex(RideId value)
{
    if (_rideIndex != value)
    {
        _rideIndex = value;
        MarkBannerTextDirty(id);
    }
}
void Banner::setTextColour(Drawing::TextColour value)
{
    if (_textColour != value)
    {
        _textColour = value;
        MarkBannerTextDirty(id);
    }
}

std::string Banner::getTextWithColour() const
{
    Formatter ft;
    formatTextWithColourTo(ft);
    return FormatStringIDLegacy(STR_STRINGID, ft.Data());
}

std::string Banner::getText() const
{
    Formatter ft;
    formatTextTo(ft);
    return FormatStringIDLegacy(STR_STRINGID, ft.Data());
}

void Banner::formatTextWithColourTo(Formatter& ft) const
{
    // Use thread_local buffer to avoid race conditions during multithreaded rendering.
    // Multiple threads can call this on the same Banner simultaneously when rendering
    // different viewport columns in parallel.
    thread_local std::string formattedTextBuffer;

    auto formatToken = FormatTokenFromTextColour(_textColour);
    formattedTextBuffer = FormatTokenToStringWithBraces(formatToken);
    ft.Add<StringId>(STR_STRING_STRINGID);
    ft.Add<const char*>(formattedTextBuffer.data());

    formatTextTo(ft);
}

void Banner::formatTextTo(Formatter& ft) const
{
    if (_flags.has(BannerFlag::noEntry))
    {
        ft.Add<StringId>(STR_NO_ENTRY);
    }
    else if (_flags.has(BannerFlag::linkedToRide))
    {
        auto ride = GetRide(_rideIndex);
        if (ride != nullptr)
        {
            ride->formatNameTo(ft);
        }
        else
        {
            ft.Add<StringId>(STR_DEFAULT_SIGN);
        }
    }
    else if (_text.empty())
    {
        ft.Add<StringId>(STR_DEFAULT_SIGN);
    }
    else
    {
        ft.Add<StringId>(STR_STRING).Add<const char*>(_text.c_str());
    }
}

/**
 *
 *  rct2: 0x006B7EAB
 */
static RideId BannerGetRideIndexAt(const CoordsXYZ& bannerCoords)
{
    RideId resultRideIndex = RideId::GetNull();
    for (auto* trackElement : TileElementsView<TrackElement>(bannerCoords))
    {
        RideId rideIndex = trackElement->getRideIndex();
        auto ride = GetRide(rideIndex);
        if (ride == nullptr || ride->getRideTypeDescriptor().flags.has(RtdFlag::isShopOrFacility))
            continue;

        if ((trackElement->getClearanceZ()) + (4 * kCoordsZStep) <= bannerCoords.z)
            continue;

        resultRideIndex = rideIndex;
    }

    return resultRideIndex;
}

static BannerIndex BannerGetNewIndex()
{
    auto& gameState = getGameState();
    for (BannerIndex::UnderlyingType bannerIndex = 0; bannerIndex < kMaxBanners; bannerIndex++)
    {
        if (bannerIndex < gameState.banners.size())
        {
            if (gameState.banners[bannerIndex].isNull())
            {
                return BannerIndex::FromUnderlying(bannerIndex);
            }
        }
        else
        {
            gameState.banners.emplace_back();
            return BannerIndex::FromUnderlying(bannerIndex);
        }
    }
    return BannerIndex::GetNull();
}

/**
 *
 *  rct2: 0x006B9CB0
 */
void BannerInit(GameState_t& gameState)
{
    gameState.banners.clear();
    ++_bannerTextEpoch;
    _dirtyBannerText.reset();
    _dirtyBannerCount = 0;
}

TileElement* BannerGetTileElement(BannerIndex bannerIndex)
{
    auto banner = GetBanner(bannerIndex);
    if (banner != nullptr)
    {
        auto tileElement = MapGetFirstElementAt(banner->position);
        if (tileElement != nullptr)
        {
            do
            {
                if (tileElement->getBannerIndex() == bannerIndex)
                {
                    return tileElement;
                }
            } while (!(tileElement++)->isLastForTile());
        }
    }
    return nullptr;
}

WallElement* BannerGetScrollingWallTileElement(BannerIndex bannerIndex)
{
    auto banner = GetBanner(bannerIndex);
    if (banner == nullptr)
        return nullptr;

    auto tileElement = MapGetFirstElementAt(banner->position);
    if (tileElement == nullptr)
        return nullptr;

    do
    {
        auto wallElement = tileElement->asWall();

        if (wallElement == nullptr)
            continue;

        auto* wallEntry = wallElement->getEntry();
        if (wallEntry->scrolling_mode == kScrollingModeNone)
            continue;
        if (wallElement->getBannerIndex() != bannerIndex)
            continue;
        return wallElement;
    } while (!(tileElement++)->isLastForTile());

    return nullptr;
}

/**
 *
 *  rct2: 0x006B7D86
 */
RideId BannerGetClosestRideIndex(const CoordsXYZ& mapPos)
{
    static constexpr std::array NeighbourCheckOrder = {
        CoordsXY{ kCoordsXYStep, 0 },
        CoordsXY{ -kCoordsXYStep, 0 },
        CoordsXY{ 0, kCoordsXYStep },
        CoordsXY{ 0, -kCoordsXYStep },
        CoordsXY{ -kCoordsXYStep, +kCoordsXYStep },
        CoordsXY{ +kCoordsXYStep, -kCoordsXYStep },
        CoordsXY{ +kCoordsXYStep, +kCoordsXYStep },
        CoordsXY{ -kCoordsXYStep, +kCoordsXYStep },
        CoordsXY{ 0, 0 },
    };

    for (const auto& neighhbourCoords : NeighbourCheckOrder)
    {
        RideId rideIndex = BannerGetRideIndexAt({ CoordsXY{ mapPos } + neighhbourCoords, mapPos.z });
        if (!rideIndex.IsNull())
        {
            return rideIndex;
        }
    }

    auto rideIndex = RideId::GetNull();
    auto resultDistance = std::numeric_limits<int32_t>::max();

    auto& gameState = getGameState();
    for (auto& ride : RideManager(gameState))
    {
        if (ride.getRideTypeDescriptor().flags.has(RtdFlag::isShopOrFacility))
            continue;

        auto rideCoords = ride.overallView;
        if (rideCoords.isNull())
            continue;

        int32_t distance = abs(mapPos.x - rideCoords.x) + abs(mapPos.y - rideCoords.y);
        if (distance < resultDistance)
        {
            resultDistance = distance;
            rideIndex = ride.id;
        }
    }
    return rideIndex;
}

struct BannerElementWithPos
{
    BannerElement* Element;
    TileCoordsXY Pos;
};

// Returns a list of BannerElement's with the tile position.
static std::vector<BannerElementWithPos> GetAllBannerElementsOnMap()
{
    auto& gameState = getGameState();
    std::vector<BannerElementWithPos> banners;
    for (int y = 0; y < gameState.mapSize.y; y++)
    {
        for (int x = 0; x < gameState.mapSize.x; x++)
        {
            const auto tilePos = TileCoordsXY{ x, y };
            for (auto* bannerElement : OpenRCT2::TileElementsView<BannerElement>(tilePos.toCoordsXY()))
            {
                auto bannerIndex = bannerElement->getIndex();
                if (bannerIndex == BannerIndex::GetNull())
                    continue;

                banners.push_back({ bannerElement, tilePos });
            }
        }
    }
    return banners;
}

// Iterates all banners and checks if the tile specified by the position actually
// has a tile with the banner index, if no tile is found then the banner element will be released.
static void BannerDeallocateUnlinked()
{
    auto& gameState = getGameState();
    for (BannerIndex::UnderlyingType index = 0; index < gameState.banners.size(); index++)
    {
        const auto bannerId = BannerIndex::FromUnderlying(index);
        auto* tileElement = BannerGetTileElement(bannerId);
        if (tileElement == nullptr)
        {
            auto* banner = GetBanner(bannerId);
            if (banner != nullptr)
            {
                banner->setType(kBannerNull);
            }
        }
    }
}

// BannerElement tiles should not share a banner entry, this iterates
// over all banner elements that shares the index and creates a new entry also
// copying the data from the current assigned banner entry.
static void BannerFixDuplicates(std::vector<BannerElementWithPos>& bannerElements)
{
    // Sort the banners by index
    std::sort(bannerElements.begin(), bannerElements.end(), [](const BannerElementWithPos& a, const BannerElementWithPos& b) {
        return a.Element->getIndex() < b.Element->getIndex();
    });

    // Create a list of all banners with duplicate indices.
    std::vector<BannerElementWithPos> duplicates;
    for (size_t i = 1; i < bannerElements.size(); i++)
    {
        if (bannerElements[i - 1].Element->getIndex() == bannerElements[i].Element->getIndex())
        {
            duplicates.push_back(bannerElements[i]);
        }
    }

    // For each duplicate, create a new banner and copy the old data
    for (const auto& duplicate : duplicates)
    {
        const auto oldIndex = duplicate.Element->getIndex();
        const auto* oldBanner = GetBanner(oldIndex);
        if (oldBanner == nullptr)
        {
            LOG_ERROR("Unable to get old banner for index %u.", oldIndex.ToUnderlying());
            continue;
        }

        auto* newBanner = CreateBanner();
        if (newBanner == nullptr)
        {
            LOG_ERROR("Failed to create new banner.");
            continue;
        }

        const auto newBannerId = newBanner->id;

        // Copy the old data to the new banner.
        *newBanner = *oldBanner;
        newBanner->id = newBannerId;

        // Assign the new banner index to the tile element.
        duplicate.Element->setIndex(newBannerId);
    }
}

// Ensures that all banner entries have the correct position based on the element
// that references the banner entry.
static void BannerFixPositions(std::vector<BannerElementWithPos>& bannerElements)
{
    for (const auto& entry : bannerElements)
    {
        const auto index = entry.Element->getIndex();
        auto* banner = GetBanner(index);
        if (banner == nullptr)
        {
            LOG_ERROR("Unable to get banner for index %u.", index.ToUnderlying());
            continue;
        }
        banner->position = entry.Pos;
    }
}

void BannerApplyFixes()
{
    auto bannerElements = GetAllBannerElementsOnMap();

    BannerFixDuplicates(bannerElements);

    BannerFixPositions(bannerElements);

    BannerDeallocateUnlinked();
}

void UnlinkAllRideBanners()
{
    auto& gameState = getGameState();
    for (auto& banner : gameState.banners)
    {
        if (!banner.isNull())
        {
            banner.setFlag(BannerFlag::linkedToRide, false);
            banner.setRideIndex(RideId::GetNull());
        }
    }
}

void UnlinkAllBannersForRide(RideId rideId)
{
    auto& gameState = getGameState();
    for (auto& banner : gameState.banners)
    {
        if (!banner.isNull() && banner.getFlags().has(BannerFlag::linkedToRide) && banner.getRideIndex() == rideId)
        {
            banner.setFlag(BannerFlag::linkedToRide, false);
            banner.setRideIndex(RideId::GetNull());
            banner.setText({});
        }
    }
}

Banner* GetBanner(BannerIndex id)
{
    auto& gameState = getGameState();
    const auto index = id.ToUnderlying();
    if (index < gameState.banners.size())
    {
        auto banner = &gameState.banners[index];
        if (banner != nullptr && !banner->isNull())
        {
            return banner;
        }
    }
    return nullptr;
}

Banner* GetOrCreateBanner(BannerIndex id)
{
    auto& gameState = getGameState();
    const auto index = id.ToUnderlying();
    if (index < kMaxBanners)
    {
        if (index >= gameState.banners.size())
        {
            gameState.banners.resize(index + 1);
        }
        // Create the banner
        auto& banner = gameState.banners[index];
        banner.id = id;
        MarkBannerTextDirty(id);
        return &banner;
    }
    return nullptr;
}

Banner* CreateBanner()
{
    auto bannerIndex = BannerGetNewIndex();
    auto banner = GetOrCreateBanner(bannerIndex);
    if (banner != nullptr)
    {
        banner->id = bannerIndex;
        banner->setFlags({});
        banner->setType(0);
        banner->setText({});
        banner->colour = OpenRCT2::Drawing::Colour::white;
        banner->setTextColour(Drawing::TextColour::white);
    }
    return banner;
}

void DeleteBanner(BannerIndex id)
{
    auto* const banner = GetBanner(id);
    if (banner != nullptr)
    {
        *banner = {};
    }
}

void TrimBanners()
{
    auto& gameState = getGameState();
    if (!gameState.banners.empty())
    {
        auto lastBannerId = gameState.banners.size() - 1;
        while (lastBannerId != std::numeric_limits<size_t>::max() && gameState.banners[lastBannerId].isNull())
        {
            lastBannerId--;
        }
        gameState.banners.resize(lastBannerId + 1);
        gameState.banners.shrink_to_fit();
    }
}

size_t GetNumBanners()
{
    auto& gameState = getGameState();
    size_t count = 0;
    for (const auto& banner : gameState.banners)
    {
        if (!banner.isNull())
        {
            count++;
        }
    }
    return count;
}

bool HasReachedBannerLimit()
{
    auto numBanners = GetNumBanners();
    return numBanners >= kMaxBanners;
}

std::shared_ptr<const WorldBannerPresentation> OpenRCT2::CaptureWorldBannerTexts(
    const std::shared_ptr<const WorldRidePresentationMaterials>& rides)
{
    // Headless simulation does not own font assets. Native rendering is unavailable there.
    if (gOpenRCT2NoGraphics)
        return nullptr;
    static std::shared_ptr<const WorldBannerPresentation> captured;
    static std::shared_ptr<const WorldRidePresentationMaterials> capturedRides;
    static uint64_t capturedEpoch{}, capturedAssets{}, nextRevision{};
    const auto assets = Drawing::ScrollingText::getAssetRevision();
    const bool reset = captured == nullptr || capturedEpoch != _bannerTextEpoch || capturedAssets != assets;
    if (!reset && _dirtyBannerCount == 0 && capturedRides == rides)
        return captured;

    auto next = captured != nullptr && !reset ? std::make_shared<WorldBannerPresentation>(*captured)
                                              : std::make_shared<WorldBannerPresentation>();
    const auto compile = [](u8string_view text) {
        return std::make_shared<const Drawing::ScrollingText::TextColumns>(
            Drawing::ScrollingText::compileTextColumns(text, Drawing::PaletteIndex::transparent));
    };
    next->banners.resize(getGameState().banners.size());
    const auto captureBanner = [&](size_t index) {
        if (index >= next->banners.size())
            return;
        const auto* banner = GetBanner(BannerIndex::FromUnderlying(static_cast<uint16_t>(index)));
        next->banners[index] = banner != nullptr ? compile(banner->getTextWithColour()) : nullptr;
    };
    if (reset)
    {
        for (size_t index = 0; index < next->banners.size(); ++index)
            captureBanner(index);
        next->queueClosed = compile(LanguageGetString(STR_RIDE_ENTRANCE_CLOSED));
    }
    else
    {
        for (size_t dirty = 0; dirty < _dirtyBannerCount; ++dirty)
            captureBanner(_dirtyBannerIds[dirty]);
    }
    if (reset || capturedRides != rides)
    {
        next->queueNames.resize(rides != nullptr ? rides->rides.size() : 0);
        for (size_t index = 0; index < next->queueNames.size(); ++index)
        {
            const auto* ride = GetRide(RideId::FromUnderlying(static_cast<uint16_t>(index)));
            if (ride == nullptr)
                next->queueNames[index].reset();
            else
            {
                auto text = compile(Drawing::ScrollingText::kRideBannerColourPrefix + ride->getName());
                if (next->queueNames[index] == nullptr || *next->queueNames[index] != *text)
                    next->queueNames[index] = std::move(text);
            }
        }
    }
    next->revision = ++nextRevision;
    captured = std::move(next);
    capturedRides = rides;
    capturedEpoch = _bannerTextEpoch;
    capturedAssets = assets;
    _dirtyBannerText.reset();
    _dirtyBannerCount = 0;
    return captured;
}
