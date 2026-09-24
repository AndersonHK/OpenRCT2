/*****************************************************************************
 * Copyright (c) 2014-2026 OpenRCT2 developers
 *
 * For a complete list of all authors, please refer to contributors.md
 * Interested in contributing? Visit https://github.com/OpenRCT2/OpenRCT2
 *
 * OpenRCT2 is licensed under the GNU General Public License version 3.
 *****************************************************************************/

#include "Viewport.h"

#include "../object/ObjectManager.h"
#ifdef OPENRCT2_VIEWPORT_PAINT_DIAGNOSTICS
    #include "ViewportPaintDiagnostics.h"

    #include <atomic>

namespace OpenRCT2::Drawing::Diagnostic
{
    static std::atomic_uint64_t GenerateCount{}, ArrangeCount{}, DrawCount{};
    void ResetViewportPaintCounts() noexcept
    {
        GenerateCount.store(0, std::memory_order_relaxed);
        ArrangeCount.store(0, std::memory_order_relaxed);
        DrawCount.store(0, std::memory_order_relaxed);
    }
    ViewportPaintCounts ReadViewportPaintCounts() noexcept
    {
        return { GenerateCount.load(std::memory_order_relaxed), ArrangeCount.load(std::memory_order_relaxed),
                 DrawCount.load(std::memory_order_relaxed) };
    }
} // namespace OpenRCT2::Drawing::Diagnostic
#endif

#include "../Context.h"
#include "../Diagnostic.h"
#include "../GameState.h"
#include "../Limits.h"
#include "../OpenRCT2.h"
#include "../config/Config.h"
#include "../core/Guard.hpp"
#include "../core/JobPool.h"
#include "../core/Numerics.hpp"
#include "../drawing/Drawing.Screen.h"
#include "../drawing/Drawing.Sprite.h"
#include "../drawing/Drawing.h"
#include "../drawing/IDrawingContext.h"
#include "../drawing/IDrawingEngine.h"
#include "../drawing/Image.h"
#include "../drawing/NewDrawing.h"
#include "../drawing/PresentationGeneration.h"
#include "../drawing/PresentationScene.h"
#include "../drawing/Rectangle.h"
#include "../drawing/RenderService.h"
#include "../drawing/WorldSelection.h"
#include "../drawing/RetainedBalloonScene.h"
#include "../entity/EntityPresentationSnapshot.h"
#include "../entity/EntityTweener.h"
#include "../entity/Guest.h"
#include "../entity/Staff.h"
#include "../interface/Cursors.h"
#include "../object/LargeSceneryEntry.h"
#include "../object/SmallSceneryEntry.h"
#include "../object/WallSceneryEntry.h"
#include "../paint/Paint.SessionFlags.h"
#include "../paint/Paint.h"
#include "../paint/entity/Paint.Vehicle.h"
#include "../platform/Platform.h"
#include "../profiling/Profiling.h"
#include "../ride/Ride.h"
#include "../ride/RideData.h"
#include "../ride/TrackDesign.h"
#include "../ride/Vehicle.h"
#include "../ui/WindowManager.h"
#include "../world/Map.h"
#include "../world/MapPresentationSnapshot.h"
#include "../world/MapSelection.h"
#include "../world/TileInspector.h"
#include "../world/Weather.h"
#include "../world/tile_element/LargeSceneryElement.h"
#include "../world/tile_element/SmallSceneryElement.h"
#include "../world/tile_element/TileElement.h"
#include "../world/tile_element/WallElement.h"
#include "Window.h"
#include "WindowBase.h"

#include <cstring>
#include <list>
#include <memory>
#include <optional>
#include <stdexcept>
#include <utility>
#include <unordered_map>

namespace OpenRCT2
{
    using namespace OpenRCT2::Drawing;
    using namespace OpenRCT2::Numerics;

    enum : uint8_t
    {
        IMAGE_TYPE_DEFAULT = 0,
        IMAGE_TYPE_REMAP = (1 << 1),
        IMAGE_TYPE_TRANSPARENT = (1 << 2),
    };

    uint8_t gShowGridLinesRefCount;
    uint8_t gShowLandRightsRefCount;
    uint8_t gShowConstructionRightsRefCount;

    static std::list<Viewport> _viewports;
    struct SecondaryViewportPreview
    {
        std::vector<std::byte> pixels;
        uint32_t image{ kImageIndexUndefined };
        uint32_t updatedAt{};
        uint32_t lastSeenDraw{};
        uint64_t requestTicket{};
        uint64_t worldEpoch{};
        int32_t width{}, height{};
        ZoomLevel zoom{};
        uint8_t rotation{};
        uint32_t flags{};
        EntityId entity{ EntityId::GetNull() };
        EntityVisualHandle entityHandle{};
        std::shared_ptr<IRenderCompletion> pending;
        std::shared_ptr<const PresentationGeneration> pendingGeneration;
        Viewport pendingViewport{};
        EntityId pendingEntity{ EntityId::GetNull() };
        EntityVisualHandle pendingEntityHandle{};
        bool recording{};
        ~SecondaryViewportPreview()
        {
            if (pending)
                pending->Cancel();
            GfxObjectFreeImages(image, 1);
        }
    };
    static std::unordered_map<const Viewport*, std::unique_ptr<SecondaryViewportPreview>> _secondaryPreviews;
    static uint64_t _nextSecondaryPreviewTicket = 1;
    static std::shared_ptr<const std::vector<uint32_t>> _frameSelection;
    Viewport* gMusicTrackingViewport;

    InteractionInfo::InteractionInfo(const PaintStruct* ps)
        : Loc(ps->MapPos)
        , Element(ps->Element)
        , interactionType(ps->InteractionItem)
    {
        if (ps->Entity)
        {
            auto* entity = getGameState().entities.getEntity(ps->Entity.id);
            if (entity != nullptr && entity->type == ps->Entity.type)
                Entity = entity;
        }
    }

    static void ViewportPaint(
        const Viewport* viewport, RenderTarget& rt, ViewportGenerationDomain domain,
        std::shared_ptr<const PresentationGeneration> auxiliaryGeneration, uint64_t selectedVehicleViewport);
    static void ViewportUpdateFollowSprite(WindowBase* window);
    static void ViewportUpdateSmartFollowEntity(WindowBase* window);
    static void ViewportUpdateSmartFollowStaff(WindowBase* window, const Staff& peep);
    static void ViewportUpdateSmartFollowVehicle(WindowBase* window);
    static void ViewportInvalidate(const Viewport* viewport, const ScreenRect& screenRect);

    /**
     * Converts between 3d point of a sprite to 2d coordinates for centring on that
     * sprite
     *  rct2: 0x006EB0C1
     * x : ax
     * y : bx
     * z : cx
     * out_x : ax
     * out_y : bx
     */
    std::optional<ScreenCoordsXY> centre2dCoordinates(const CoordsXYZ& loc, Viewport* viewport)
    {
        // If the start location was invalid
        // propagate the invalid location to the output.
        // This fixes a bug that caused the game to enter an infinite loop.
        if (loc.isNull())
        {
            return std::nullopt;
        }

        auto screenCoord = Translate3DTo2DWithZ(viewport->rotation, loc);
        screenCoord.x -= viewport->ViewWidth() / 2;
        screenCoord.y -= viewport->ViewHeight() / 2;
        return { screenCoord };
    }

    CoordsXYZ Focus::GetPos() const
    {
        return std::visit(
            [](auto&& arg) {
                using T = std::decay_t<decltype(arg)>;
                if constexpr (std::is_same_v<T, CoordinateFocus>)
                    return arg;
                else if constexpr (std::is_same_v<T, EntityFocus>)
                {
                    auto* centreEntity = getGameState().entities.getEntity(arg);
                    if (centreEntity != nullptr)
                    {
                        return CoordsXYZ{ centreEntity->x, centreEntity->y, centreEntity->z };
                    }
                    else
                    {
                        LOG_ERROR("Invalid entity for focus.");
                        return CoordsXYZ{};
                    }
                }
            },
            data);
    }

    /**
     * Viewport will look at sprite or at coordinates as specified in flags 0b_1X
     * for sprite 0b_0X for coordinates
     *
     *  rct2: 0x006EB009
     *  x:      ax
     *  y:      eax (top 16)
     *  width:  bx
     *  height: ebx (top 16)
     *  zoom:   cl (8 bits)
     *  centre_x: edx lower 16 bits
     *  centre_y: edx upper 16 bits
     *  centre_z: ecx upper 16 bits
     *  sprite: edx lower 16 bits
     *  flags:  edx top most 2 bits 0b_X1 for zoom clear see below for 2nd bit.
     *  w:      esi
     */
    void ViewportCreate(WindowBase& w, const ScreenCoordsXY& screenCoords, int32_t width, int32_t height, const Focus& focus)
    {
        Viewport* viewport = nullptr;
        if (_viewports.size() >= kMaxViewportCount)
        {
            LOG_ERROR("No more viewport slots left to allocate.");
            return;
        }

        auto itViewport = _viewports.insert(_viewports.end(), Viewport{});

        viewport = &*itViewport;
        viewport->pos = screenCoords;
        viewport->width = width;
        viewport->height = height;

        const auto zoom = focus.zoom;
        viewport->zoom = zoom;
        viewport->flags = 0;
        viewport->rotation = GetCurrentRotation();

        if (Config::Get().general.alwaysShowGridlines)
            viewport->flags |= VIEWPORT_FLAG_GRIDLINES;
        w.viewport = viewport;
        viewport->isVisible = w.isVisible;

        CoordsXYZ centrePos = focus.GetPos();
        w.viewportTargetSprite = std::visit(
            [](auto&& arg) {
                using T = std::decay_t<decltype(arg)>;
                if constexpr (std::is_same_v<T, Focus::CoordinateFocus>)
                    return EntityId::GetNull();
                else if constexpr (std::is_same_v<T, Focus::EntityFocus>)
                    return arg;
            },
            focus.data);

        auto centreLoc = centre2dCoordinates(centrePos, viewport);
        if (!centreLoc.has_value())
        {
            LOG_ERROR("Invalid location for viewport.");
            return;
        }
        w.savedViewPos = *centreLoc;
        viewport->viewPos = *centreLoc;
    }

    void ViewportRemove(Viewport* viewport)
    {
        _secondaryPreviews.erase(viewport);
        auto it = std::find_if(_viewports.begin(), _viewports.end(), [viewport](const auto& vp) { return &vp == viewport; });
        if (it == _viewports.end())
        {
            LOG_ERROR("Unable to remove viewport: %p", viewport);
            return;
        }
        _viewports.erase(it);
    }

    static Viewport* ViewportGetMain()
    {
        auto mainWindow = WindowGetMain();
        if (mainWindow == nullptr)
        {
            return nullptr;
        }
        return mainWindow->viewport;
    }

    void ViewportsInvalidate(const int32_t x, const int32_t y, const int32_t z0, const int32_t z1, const ZoomLevel maxZoom)
    {
        if (DrawingEngineCanSkipViewportInvalidation())
            return;

        for (const auto& viewport : _viewports)
        {
            if (viewport.isVisible)
            {
                viewport.Invalidate(x, y, z0, z1, maxZoom);
            }
        }
    }

    void ViewportsInvalidate(
        const CoordsXYZ& pos, const int32_t width, const int32_t minHeight, const int32_t maxHeight, const ZoomLevel maxZoom)
    {
        if (DrawingEngineCanSkipViewportInvalidation())
            return;

        for (auto& vp : _viewports)
        {
            if (vp.isVisible && (maxZoom == ZoomLevel{ -1 } || vp.zoom <= ZoomLevel{ maxZoom }))
            {
                auto screenCoords = Translate3DTo2DWithZ(vp.rotation, pos);
                auto screenPos = ScreenRect(
                    screenCoords - ScreenCoordsXY{ width, minHeight }, screenCoords + ScreenCoordsXY{ width, maxHeight });

                ViewportInvalidate(&vp, screenPos);
            }
        }
    }

    void ViewportsInvalidate(const ScreenRect& screenRect, const ZoomLevel maxZoom)
    {
        if (DrawingEngineCanSkipViewportInvalidation())
            return;

        for (auto& vp : _viewports)
        {
            if (vp.isVisible && (maxZoom == ZoomLevel{ -1 } || vp.zoom <= ZoomLevel{ maxZoom }))
            {
                ViewportInvalidate(&vp, screenRect);
            }
        }
    }

    /**
     *
     *  rct2: 0x00689174
     * edx is assumed to be (and always is) the current rotation, so it is not
     * needed as parameter.
     */
    CoordsXYZ ViewportAdjustForMapHeight(const ScreenCoordsXY& startCoords, uint8_t rotation)
    {
        int32_t height = 0;

        CoordsXY pos{};
        for (int32_t i = 0; i < 6; i++)
        {
            pos = ViewportPosToMapPos(startCoords, height, rotation);
            height = TileElementHeight(pos);

            // HACK: This is to prevent the x and y values being set to values outside
            // of the map. This can happen when the height is larger than the map size.
            auto mapSizeMinus2 = GetMapSizeMinus2();
            if (pos.x > mapSizeMinus2.x && pos.y > mapSizeMinus2.y)
            {
                static constexpr CoordsXY kCorr[] = {
                    { -1, -1 },
                    { 1, -1 },
                    { 1, 1 },
                    { -1, 1 },
                };
                pos.x += kCorr[rotation].x * height;
                pos.y += kCorr[rotation].y * height;
            }
        }

        return { pos, height };
    }

    static void ViewportMove(const ScreenCoordsXY& coords, Viewport* viewport)
    {
        if (viewport->viewPos == coords)
            return;
        viewport->viewPos = coords;
        // Camera motion changes a global parameter. Every GPU frame redraws the world;
        // there are no pixel shifts, exposed strips or provisional paint callbacks.
        Drawing::GfxInvalidateScreen();
    }

    // rct2: 0x006E7A15
    static void ViewportSetUndergroundFlag(int32_t underground, WindowBase* window, Viewport* viewport)
    {
        if ((window->classification != WindowClass::mainWindow && window->classification != WindowClass::viewport)
            || (window->classification == WindowClass::mainWindow && !window->viewportSmartFollowSprite.IsNull()))
        {
            if (!underground)
            {
                int32_t bit = viewport->flags & VIEWPORT_FLAG_UNDERGROUND_INSIDE;
                viewport->flags &= ~VIEWPORT_FLAG_UNDERGROUND_INSIDE;
                if (!bit)
                    return;
            }
            else
            {
                int32_t bit = viewport->flags & VIEWPORT_FLAG_UNDERGROUND_INSIDE;
                viewport->flags |= VIEWPORT_FLAG_UNDERGROUND_INSIDE;
                if (bit)
                    return;
            }
            window->invalidate();
        }
    }

    /**
     *
     *  rct2: 0x006E7A3A
     */
    void ViewportUpdatePosition(WindowBase* window)
    {
        // Guard against any code attempting to call this at the wrong time.
        Guard::Assert(
            GetContext()->GetDrawingEngine()->GetDrawingContext() != nullptr, "We must be in a valid drawing context.");

        Viewport* viewport = window->viewport;
        if (viewport == nullptr)
            return;

        if (!window->viewportSmartFollowSprite.IsNull())
        {
            ViewportUpdateSmartFollowEntity(window);
        }

        if (!window->viewportTargetSprite.IsNull())
        {
            ViewportUpdateFollowSprite(window);
            return;
        }

        ViewportSetUndergroundFlag(0, window, viewport);

        auto viewportMidPoint = ScreenCoordsXY{ window->savedViewPos.x + viewport->ViewWidth() / 2,
                                                window->savedViewPos.y + viewport->ViewHeight() / 2 };

        auto mapCoord = ViewportPosToMapPos(viewportMidPoint, 0, viewport->rotation);

        // Clamp to the map minimum value
        int32_t at_map_edge = 0;
        if (mapCoord.x < kMapMinimumXY)
        {
            mapCoord.x = kMapMinimumXY;
            at_map_edge = 1;
        }
        if (mapCoord.y < kMapMinimumXY)
        {
            mapCoord.y = kMapMinimumXY;
            at_map_edge = 1;
        }

        // Clamp to the map maximum value (scenario specific)
        auto mapSizeMinus2 = GetMapSizeMinus2();
        if (mapCoord.x > mapSizeMinus2.x)
        {
            mapCoord.x = mapSizeMinus2.x;
            at_map_edge = 1;
        }
        if (mapCoord.y > mapSizeMinus2.y)
        {
            mapCoord.y = mapSizeMinus2.y;
            at_map_edge = 1;
        }

        if (at_map_edge)
        {
            auto centreLoc = centre2dCoordinates({ mapCoord, 0 }, viewport);
            if (centreLoc.has_value())
            {
                window->savedViewPos = centreLoc.value();
            }
        }

        auto windowCoords = window->savedViewPos;
        if (window->flags.has(WindowFlag::scrollingToLocation))
        {
            // Moves the viewport if focusing in on an item
            uint8_t flags = 0;
            windowCoords.x -= viewport->viewPos.x;
            if (windowCoords.x < 0)
            {
                windowCoords.x = -windowCoords.x;
                flags |= 1;
            }
            windowCoords.y -= viewport->viewPos.y;
            if (windowCoords.y < 0)
            {
                windowCoords.y = -windowCoords.y;
                flags |= 2;
            }
            windowCoords.x = (windowCoords.x + 7) / 8;
            windowCoords.y = (windowCoords.y + 7) / 8;

            // If we are at the final zoom position
            if (!windowCoords.x && !windowCoords.y)
            {
                window->flags.unset(WindowFlag::scrollingToLocation);
            }
            if (flags & 1)
            {
                windowCoords.x = -windowCoords.x;
            }
            if (flags & 2)
            {
                windowCoords.y = -windowCoords.y;
            }
            windowCoords.x += viewport->viewPos.x;
            windowCoords.y += viewport->viewPos.y;
        }

        ViewportMove(windowCoords, viewport);
    }

    void ViewportUpdateFollowSprite(WindowBase* window)
    {
        if (!window->viewportTargetSprite.IsNull() && window->viewport != nullptr)
        {
            auto* sprite = getGameState().entities.getEntity(window->viewportTargetSprite);
            if (sprite == nullptr)
            {
                return;
            }

            if (gLegacyScene != LegacyScene::titleSequence)
            {
                int32_t height = TileElementHeight({ sprite->x, sprite->y }) - 16;
                int32_t underground = sprite->z < height;
                ViewportSetUndergroundFlag(underground, window, window->viewport);
            }

            auto centreLoc = centre2dCoordinates(sprite->getLocation(), window->viewport);
            if (centreLoc.has_value())
            {
                window->savedViewPos = *centreLoc;
                ViewportMove(*centreLoc, window->viewport);
            }
        }
    }

    void ViewportUpdateSmartFollowEntity(WindowBase* window)
    {
        auto entity = getGameState().entities.tryGetEntity(window->viewportSmartFollowSprite);
        if (entity == nullptr || entity->type == EntityType::null)
        {
            window->viewportSmartFollowSprite = EntityId::GetNull();
            window->viewportTargetSprite = EntityId::GetNull();
            return;
        }

        switch (entity->type)
        {
            case EntityType::vehicle:
                ViewportUpdateSmartFollowVehicle(window);
                break;

            case EntityType::guest:
            {
                auto* guest = entity->as<Guest>();
                if (guest == nullptr)
                {
                    return;
                }
                ViewportUpdateSmartFollowGuest(window, *guest);
                break;
            }
            case EntityType::staff:
            {
                auto* staff = entity->as<Staff>();
                if (staff == nullptr)
                {
                    return;
                }
                ViewportUpdateSmartFollowStaff(window, *staff);
                break;
            }
            default: // All other types don't need any "smart" following; steam particle, duck, money effect, etc.
                window->focus = Focus(window->viewportSmartFollowSprite);
                window->viewportTargetSprite = window->viewportSmartFollowSprite;
                break;
        }
    }

    void ViewportUpdateSmartFollowGuest(WindowBase* window, const Guest& peep)
    {
        Focus focus = Focus(peep.id);
        window->viewportTargetSprite = peep.id;

        if (peep.state == PeepState::picked)
        {
            window->viewportSmartFollowSprite = EntityId::GetNull();
            window->viewportTargetSprite = EntityId::GetNull();
            window->focus = std::nullopt; // No focus
            return;
        }

        bool overallFocus = true;
        if (peep.state == PeepState::onRide || peep.state == PeepState::enteringRide
            || (peep.state == PeepState::leavingRide && peep.x == kLocationNull))
        {
            auto ride = GetRide(peep.currentRide);
            if (ride != nullptr && ride->flags.has(RideFlag::onTrack))
            {
                auto train = getGameState().entities.getEntity<Vehicle>(ride->vehicles[peep.currentTrain]);
                if (train != nullptr)
                {
                    const auto car = train->GetCar(peep.currentCar);
                    if (car != nullptr)
                    {
                        focus = Focus(car->id);
                        overallFocus = false;
                        window->viewportTargetSprite = car->id;
                    }
                }
            }
        }

        if (peep.x == kLocationNull && overallFocus)
        {
            auto ride = GetRide(peep.currentRide);
            if (ride != nullptr)
            {
                auto xy = ride->overallView.toTileCentre();
                CoordsXYZ coordFocus;
                coordFocus.x = xy.x;
                coordFocus.y = xy.y;
                coordFocus.z = TileElementHeight(xy) + (4 * kCoordsZStep);
                focus = Focus(coordFocus);
                window->viewportTargetSprite = EntityId::GetNull();
            }
        }

        window->focus = focus;
    }

    void ViewportUpdateSmartFollowStaff(WindowBase* window, const Staff& peep)
    {
        if (peep.state == PeepState::picked)
        {
            window->viewportSmartFollowSprite = EntityId::GetNull();
            window->viewportTargetSprite = EntityId::GetNull();
            window->focus = std::nullopt;
            return;
        }

        window->focus = Focus(window->viewportSmartFollowSprite);
        window->viewportTargetSprite = window->viewportSmartFollowSprite;
    }

    void ViewportUpdateSmartFollowVehicle(WindowBase* window)
    {
        window->focus = Focus(window->viewportSmartFollowSprite);
        window->viewportTargetSprite = window->viewportSmartFollowSprite;
    }

    static void ViewportRotateSingleInternal(WindowBase& w, int32_t direction)
    {
        auto* viewport = w.viewport;
        if (viewport == nullptr)
            return;

        auto windowPos = ScreenCoordsXY{ (viewport->width >> 1), (viewport->height >> 1) } + viewport->pos;

        // has something to do with checking if middle of the viewport is obstructed
        Viewport* other;
        auto mapXYCoords = ScreenGetMapXY(windowPos, &other);
        CoordsXYZ coords{};

        // other != viewport probably triggers on viewports in ride or guest window?
        // mapXYCoords is nullopt if middle of viewport is obstructed by another window?
        if (!mapXYCoords.has_value() || other != viewport)
        {
            auto viewPos = ScreenCoordsXY{ (viewport->ViewWidth() >> 1), (viewport->ViewHeight() >> 1) } + viewport->viewPos;

            coords = ViewportAdjustForMapHeight(viewPos, viewport->rotation);
        }
        else
        {
            coords.x = mapXYCoords->x;
            coords.y = mapXYCoords->y;
            coords.z = TileElementHeight(coords);
        }

        viewport->rotation = (viewport->rotation + direction) & 3;

        auto centreLoc = centre2dCoordinates(coords, viewport);

        if (centreLoc.has_value())
        {
            w.savedViewPos = centreLoc.value();
            viewport->viewPos = *centreLoc;
        }

        w.invalidate();
        w.onViewportRotate();
    }

    void ViewportRotateSingle(WindowBase* window, int32_t direction)
    {
        ViewportRotateSingleInternal(*window, direction);
    }

    void ViewportRotateAll(int32_t direction)
    {
        WindowVisitEach([direction](WindowBase* w) {
            auto* viewport = w->viewport;
            if (viewport == nullptr)
                return;
            if (viewport->flags & VIEWPORT_FLAG_INDEPENDENT_ROTATION)
                return;
            ViewportRotateSingleInternal(*w, direction);
        });
    }

    /**
     *
     *  rct2: 0x00685C02
     *  ax: left
     *  bx: top
     *  dx: right
     *  esi: viewport
     *  edi: rt
     *  ebp: bottom
     */
    void ViewportRender(RenderTarget& rt, const Viewport* viewport)
    {
        ViewportRender(rt, viewport, ViewportGenerationDomain::targetClip);
    }

    void ViewportRender(RenderTarget& rt, const Viewport* viewport, ViewportGenerationDomain domain)
    {
        ViewportRender(rt, viewport, domain, {});
    }

    void ViewportRender(
        RenderTarget& rt, const Viewport* viewport, ViewportGenerationDomain domain,
        std::shared_ptr<const PresentationGeneration> auxiliaryGeneration, uint64_t selectedVehicleViewport)
    {
        if (viewport->flags & VIEWPORT_FLAG_RENDERING_INHIBITED)
            return;

        if (rt.x + rt.width <= viewport->pos.x)
            return;
        if (rt.y + rt.height <= viewport->pos.y)
            return;
        if (rt.x >= viewport->pos.x + viewport->width)
            return;
        if (rt.y >= viewport->pos.y + viewport->height)
            return;

        ViewportPaint(viewport, rt, domain, std::move(auxiliaryGeneration), selectedVehicleViewport);
    }

    static PresentationScene& GetPresentationScene()
    {
        thread_local PresentationScene scene;
        return scene;
    }

    void ViewportBeginPresentationFrame()
    {
        std::vector<SelectedVehicleRequest> vehicleRequests;
        const auto now = Platform::GetTicks();
        WindowVisitEach([&](WindowBase* window) {
            const auto* viewport = window->viewport;
            if (viewport == nullptr || viewport == ViewportGetMain()
                || getGameState().entities.getEntity<Vehicle>(window->viewportTargetSprite) == nullptr)
                return;
            const auto found = _secondaryPreviews.find(viewport);
            if (found != _secondaryPreviews.end())
            {
                const auto& preview = *found->second;
                if (preview.pending || (preview.image != kImageIndexUndefined
                    && static_cast<uint32_t>(now - preview.updatedAt) < 34
                    && preview.entityHandle == getGameState().entities.GetEntityVisualHandle(window->viewportTargetSprite)
                    && preview.zoom == viewport->zoom && preview.rotation == viewport->rotation
                    && preview.flags == viewport->flags))
                    return;
            }
            vehicleRequests.push_back({ .viewport = reinterpret_cast<uintptr_t>(viewport),
                .entity = getGameState().entities.GetEntityVisualHandle(window->viewportTargetSprite),
                .viewFlags = viewport->flags, .zoom = static_cast<int8_t>(viewport->zoom), .rotation = viewport->rotation,
                .clipHeight = gClipHeight, .clipFirst = gClipSelectionA, .clipLast = gClipSelectionB });
        });
        GetPresentationScene().SetSelectedVehicleRequests(std::move(vehicleRequests));
        if (gMapSelectFlags.isEmpty())
            _frameSelection.reset();
        else
        {
            auto words = MakeWorldSelectionWords(
                gMapSelectFlags.holder, EnumValue(gMapSelectType), gMapSelectPositionA, gMapSelectPositionB,
                gMapSelectArrowPosition, gMapSelectArrowDirection, MapSelection::getSelectedTiles());
            if (!_frameSelection || *_frameSelection != words)
                _frameSelection = std::make_shared<const std::vector<uint32_t>>(std::move(words));
        }
        auto& gameState = getGameState();
        auto& jobs = GetContext()->GetJobPool();

        // Legacy diagnostics may request an immediate edit snapshot. Native terrain always
        // queues owned changes and admits them at a later frame boundary without waiting.
        const bool requiresSynchronousMapPublication = !gMapSelectFlags.isEmpty()
            || TileInspector::GetSelectedElement() != nullptr || isToolActive(WindowClass::trackDesignPlace);
        const auto* engine = GetContext()->GetDrawingEngine();
        const auto profile = engine == nullptr ? EntityPublicationProfile::gpuTerrainOnly
                                               : engine->GetEntityPublicationProfile();
        const bool needsPeepCatalog = profile == EntityPublicationProfile::nativePeeps
            || profile == EntityPublicationProfile::retainedPeepsAndBalloons;
        auto catalog = needsPeepCatalog ? GetContext()->GetObjectManager().GetPeepAnimationCatalog() : nullptr;
        if (GetPresentationScene().BeginFrame(
                jobs, gameState.entities, gCurrentDrawCount, requiresSynchronousMapPublication, profile, catalog))
        {
            // UI invalidation does not choose another world rendering path.
            GfxInvalidateScreen();
        }
        // Capture the next immutable state at this same owner boundary. Its worker
        // may finish during painting, but only the next frame may publish it.
        GetPresentationScene().ScheduleNext(jobs, gameState.entities, std::move(catalog));
    }

    std::shared_ptr<const PresentationGeneration> ViewportGetPresentationGeneration()
    {
        return GetPresentationScene().GetGeneration();
    }

    std::shared_ptr<const PresentationGeneration> ViewportCaptureAuxiliaryGeneration()
    {
        auto map = CaptureAuxiliaryMapPresentationSnapshot();
        auto entities = std::make_shared<EntityPresentationSnapshot>();
        entities->CaptureNativeStorage(getGameState().entities, {}, {});
        if (map->GetSourceTick() != entities->GetSourceTick())
            throw std::logic_error("Auxiliary map and entity snapshot ticks differ");
        return std::make_shared<PresentationGeneration>(PresentationGeneration{
            .map = std::move(map),
            .entities = entities,
            .sourceTick = entities->GetSourceTick(),
            .sourceEntityEpoch = entities->GetSourceEpoch(),
        });
    }

    Drawing::BalloonPublicationCopyTotals ViewportGetBalloonPublicationCopyTotals()
    {
        return GetPresentationScene().GetBalloonPublicationCopyTotals();
    }

    void ViewportDisposePresentation()
    {
        _secondaryPreviews.clear();
        GetPresentationScene().Reset(GetContext()->GetJobPool());
    }

    static void DrawSecondaryViewport(
        RenderTarget& rt, const Viewport& viewport, const std::shared_ptr<const PresentationGeneration>& generation)
    {
        if (viewport.width <= 0 || viewport.height <= 0 || viewport.width > 32767 || viewport.height > 32767
            || static_cast<uint64_t>(viewport.width) * viewport.height > 4 * 1024 * 1024)
            throw RenderServiceException({ RenderErrorCode::invalidRequest, "Secondary viewport exceeds bounded preview extent" });
        EntityId selected = EntityId::GetNull();
        WindowVisitEach([&](WindowBase* window) {
            if (window->viewport == &viewport)
                selected = window->viewportTargetSprite;
        });
        const auto selectedHandle = selected.IsNull() ? EntityVisualHandle{}
                                                     : getGameState().entities.GetEntityVisualHandle(selected);
        auto& entry = _secondaryPreviews[&viewport];
        if (!entry)
            entry = std::make_unique<SecondaryViewportPreview>();
        auto& preview = *entry;
        preview.lastSeenDraw = gCurrentDrawCount;
        const auto now = Platform::GetTicks();
        if (preview.recording)
            return; // Asset discovery can pump progress drawing; do not re-enter an active auxiliary session.
        preview.recording = true;
        struct RecordingScope { bool& active; ~RecordingScope() { active = false; } } recording{ preview.recording };
        if (preview.pending)
        {
            const auto outcome = preview.pending->Wait(std::chrono::milliseconds(0));
            if (!outcome.error || outcome.error->code != RenderErrorCode::timeout)
            {
                if (outcome.error)
                    throw RenderServiceException(*outcome.error);
                const auto& captured = preview.pendingViewport;
                const auto expectedPixels = static_cast<size_t>(captured.width) * captured.height;
                if (!outcome.result || outcome.identity != preview.pending->GetIdentity()
                    || outcome.result->identity != outcome.identity || outcome.result->indexed.size() != expectedPixels)
                    throw RenderServiceException({ RenderErrorCode::executionFailed, "Secondary viewport readback differs" });
                // A resized/retargeted window must never display an obsolete result in its new geometry.
                if (captured.width == viewport.width && captured.height == viewport.height
                    && captured.zoom == viewport.zoom && captured.rotation == viewport.rotation
                    && captured.flags == viewport.flags && preview.pendingEntity == selected
                    && preview.pendingEntityHandle == selectedHandle
                    && preview.pendingGeneration->map->GetEpoch() == generation->map->GetEpoch())
                {
                    auto pixels = outcome.result->indexed;
                    G1Element sprite{};
                    sprite.offset = reinterpret_cast<uint8_t*>(pixels.data());
                    sprite.width = static_cast<int16_t>(captured.width);
                    sprite.height = static_cast<int16_t>(captured.height);
                    if (preview.image == kImageIndexUndefined)
                        preview.image = GfxObjectAllocateImages(&sprite, 1);
                    else
                    {
                        GfxSetG1Element(preview.image, &sprite);
                        DrawingEngineInvalidateImage(preview.image);
                    }
                    if (preview.image == kImageIndexUndefined)
                        throw RenderServiceException({ RenderErrorCode::creationFailed, "Secondary viewport image allocation failed" });
                    preview.pixels = std::move(pixels);
                    preview.worldEpoch = preview.pendingGeneration->map->GetEpoch();
                    preview.width = captured.width;
                    preview.height = captured.height;
                    preview.zoom = captured.zoom;
                    preview.rotation = captured.rotation;
                    preview.flags = captured.flags;
                    preview.entity = selected;
                    preview.entityHandle = selectedHandle;
                }
                preview.pending.reset();
                preview.pendingGeneration.reset();
            }
        }
        const bool changed = preview.image == kImageIndexUndefined || preview.width != viewport.width
            || preview.height != viewport.height || preview.zoom != viewport.zoom || preview.rotation != viewport.rotation
            || preview.flags != viewport.flags || preview.entityHandle != selectedHandle
            || preview.worldEpoch != generation->map->GetEpoch();
        // Auxiliary bitmap previews are intentionally rate limited. Main-world rendering never reads these pixels.
        const bool vehicleTarget = getGameState().entities.getEntity<Vehicle>(selected) != nullptr;
        bool coherentVehicle = !vehicleTarget;
        if (vehicleTarget && generation->selectedVehicles != nullptr)
        {
            const auto handle = getGameState().entities.GetEntityVisualHandle(selected);
            coherentVehicle = std::any_of(generation->selectedVehicles->views.begin(), generation->selectedVehicles->views.end(),
                [&](const auto& view) {
                    return view.request.viewport == reinterpret_cast<uintptr_t>(&viewport) && view.request.entity == handle
                        && view.request.rotation == viewport.rotation && view.request.zoom == static_cast<int8_t>(viewport.zoom)
                        && view.request.viewFlags == viewport.flags && view.request.clipHeight == gClipHeight
                        && view.request.clipFirst == gClipSelectionA && view.request.clipLast == gClipSelectionB;
                });
        }
        const bool due = coherentVehicle && !preview.pending
            && (changed || static_cast<uint32_t>(now - preview.updatedAt) >= 34);
        if (!due)
            preview.requestTicket = 0;
        if (due && preview.requestTicket == 0)
            preview.requestTicket = _nextSecondaryPreviewTicket++;
        const bool turn = std::none_of(_secondaryPreviews.begin(), _secondaryPreviews.end(), [&](const auto& item) {
            const auto& other = *item.second;
            return other.requestTicket != 0 && other.requestTicket < preview.requestTicket
                && static_cast<uint32_t>(gCurrentDrawCount - other.lastSeenDraw) <= 1;
        });
        if (due && turn)
        {
            OffscreenRenderRequest request;
            request.name = "secondary-viewport";
            request.logicalExtent = request.outputExtent = {
                static_cast<uint32_t>(viewport.width), static_cast<uint32_t>(viewport.height) };
            request.clearIndex = EnumValue(PaletteIndex::pi10);
            auto session = GetContext()->GetRenderService().TryBeginOffscreen(std::move(request));
            if (session)
            {
                auto& target = session->GetRenderTarget();
                Viewport isolated = viewport;
                isolated.pos = { 0, 0 };
                ViewportRender(target, &isolated, ViewportGenerationDomain::targetClip, generation,
                    vehicleTarget ? reinterpret_cast<uintptr_t>(&viewport) : 0);
                preview.pending = session->Submit();
                if (!preview.pending)
                    throw RenderServiceException({ RenderErrorCode::executionFailed, "Secondary viewport has no completion" });
                preview.pendingGeneration = generation;
                preview.pendingViewport = viewport;
                preview.pendingEntity = selected;
                preview.pendingEntityHandle = selectedHandle;
                preview.updatedAt = now;
                preview.requestTicket = 0;
            }
        }
        if (preview.image != kImageIndexUndefined && preview.width == viewport.width && preview.height == viewport.height
            && preview.zoom == viewport.zoom && preview.rotation == viewport.rotation && preview.flags == viewport.flags
            && preview.entityHandle == selectedHandle && preview.worldEpoch == generation->map->GetEpoch())
            GfxDrawSprite(rt, ImageId(preview.image), viewport.pos);
    }

    /**
     *
     *  rct2: 0x00685CBF
     *  eax: left
     *  ebx: top
     *  edx: right
     *  esi: viewport
     *  edi: rt
     *  ebp: bottom
     */
    static void ViewportPaint(
        const Viewport* viewport, RenderTarget& rt, [[maybe_unused]] ViewportGenerationDomain domain,
        std::shared_ptr<const PresentationGeneration> auxiliaryGeneration, uint64_t selectedVehicleViewport)
    {
        PROFILED_FUNCTION();
        // The viewport has one world renderer. Missing GPU families deliberately remain absent while
        // this replacement is built; there is no CPU paint-session generation, ordering or fallback.
        const bool mainPresentation = rt.DrawingEngine == GetContext()->GetDrawingEngine();
        auto& presentation = GetPresentationScene();
        const auto generation = mainPresentation ? presentation.GetGeneration()
            : auxiliaryGeneration ? std::move(auxiliaryGeneration) : ViewportCaptureAuxiliaryGeneration();
        const bool terrainOnly = generation && generation->entities && generation->entities->IsTerrainOnly();
        const bool terrainOnlyMain = mainPresentation && terrainOnly && viewport == ViewportGetMain();
        const bool terrainOnlySecondary = mainPresentation && terrainOnly && !terrainOnlyMain;
        if (terrainOnlySecondary)
        {
            DrawSecondaryViewport(rt, *viewport, generation);
            return;
        }
        // Window drawing may split the main viewport around opaque UI. Record one full main-world
        // background, clipped only to its viewport and the engine target; later UI remains above it.
        const auto* source = terrainOnlyMain ? rt.DrawingEngine->getRT() : &rt;
        if (source == nullptr)
            throw std::runtime_error("GPU main viewport has no engine render target");
        const auto& sceneRT = *source;

        const int32_t offsetX = sceneRT.x - viewport->pos.x;
        const int32_t offsetY = sceneRT.y - viewport->pos.y;
        RenderTarget worldRT;
        worldRT.DrawingEngine = sceneRT.DrawingEngine;
        worldRT.bits = sceneRT.bits == nullptr
            ? nullptr
            : sceneRT.bits + std::max(0, -offsetX) + std::max(0, -offsetY) * sceneRT.LineStride();
        worldRT.x = viewport->zoom.ApplyInversedTo(viewport->viewPos.x) + std::max(0, offsetX);
        worldRT.y = viewport->zoom.ApplyInversedTo(viewport->viewPos.y) + std::max(0, offsetY);
        worldRT.width = std::min(viewport->pos.x + viewport->width, sceneRT.x + sceneRT.width)
            - std::max(viewport->pos.x, sceneRT.x);
        worldRT.height = std::min(viewport->pos.y + viewport->height, sceneRT.y + sceneRT.height)
            - std::max(viewport->pos.y, sceneRT.y);
        worldRT.pitch = sceneRT.LineStride() - worldRT.width;
        worldRT.zoom_level = viewport->zoom;
        if (worldRT.width <= 0 || worldRT.height <= 0)
            return;
        if (!terrainOnlyMain && !(viewport->flags & VIEWPORT_FLAG_TRANSPARENT_BACKGROUND))
            GfxClear(worldRT, PaletteIndex::pi10);

        auto* context = rt.DrawingEngine->GetDrawingContext();
        if (context == nullptr)
            throw std::runtime_error("Viewport requires the Vulkan world context");
        if (generation != nullptr && !terrainOnlySecondary)
        {
            const OrthographicCamera camera{
                .viewX = worldRT.x,
                .viewY = worldRT.y,
                .clipLeft = std::max(viewport->pos.x, sceneRT.x),
                .clipTop = std::max(viewport->pos.y, sceneRT.y),
                .clipRight = std::min(viewport->pos.x + viewport->width, sceneRT.x + sceneRT.width),
                .clipBottom = std::min(viewport->pos.y + viewport->height, sceneRT.y + sceneRT.height),
                .zoom = static_cast<int8_t>(viewport->zoom),
                .rotation = viewport->rotation,
                .landscapeSmoothing = Config::Get().general.landscapeSmoothing,
                .nativeEntitiesAllowed = viewport->flags == 0 && !gPaintStableSort && !gPaintBoundingBoxes
                    && !gPaintBlockedTiles && !gTrackDesignSaveMode,
                .entityInterpolation = EntityTweener::get().GetRenderAlpha(),
                .entityInterpolationSourceTick = getGameState().currentTicks,
                .viewFlags = viewport->flags,
                .selection = terrainOnlyMain ? _frameSelection : nullptr,
                .selectedVehicleViewport = selectedVehicleViewport,
            };
            const auto world = context->DrawWorldScene(worldRT, generation, camera);
            if (!world.completeTerrainScene)
                throw std::runtime_error("GPU world renderer rejected its published scene");
            context->SealWorldScene(world);
        }
        // Auxiliary sessions own an independent immutable generation and the same native GPU world path.
    }

    /**
     *
     *  rct2: 0x0068958D
     */
    std::optional<CoordsXY> ScreenPosToMapPos(const ScreenCoordsXY& screenCoords, int32_t* direction)
    {
        auto mapCoords = ScreenGetMapXY(screenCoords, nullptr);
        if (!mapCoords.has_value())
            return std::nullopt;

        int32_t my_direction;
        int32_t dist_from_centre_x = abs(mapCoords->x % 32);
        int32_t dist_from_centre_y = abs(mapCoords->y % 32);
        if (dist_from_centre_x > 8 && dist_from_centre_x < 24 && dist_from_centre_y > 8 && dist_from_centre_y < 24)
        {
            my_direction = 4;
        }
        else
        {
            auto mod_x = mapCoords->x & 0x1F;
            auto mod_y = mapCoords->y & 0x1F;
            if (mod_x <= 16)
            {
                if (mod_y < 16)
                {
                    my_direction = 2;
                }
                else
                {
                    my_direction = 3;
                }
            }
            else
            {
                if (mod_y < 16)
                {
                    my_direction = 1;
                }
                else
                {
                    my_direction = 0;
                }
            }
        }

        if (direction != nullptr)
            *direction = my_direction;
        return { mapCoords->toTileStart() };
    }

    [[nodiscard]] bool Viewport::ContainsTile(const TileCoordsXY coords) const noexcept
    {
        const auto centreCoords = coords.toCoordsXY() + CoordsXY(kCoordsXYHalfTile, kCoordsXYHalfTile);
        const auto screenPos = Translate3DTo2DWithZ(rotation, CoordsXYZ{ centreCoords, 0 });
        const auto left = screenPos.x - kScreenCoordsTileWidthHalf;
        const auto top = screenPos.y - (kMaxTileElementHeight * kCoordsZStep) - kScreenCoordsTileHeightHalf;
        const auto right = screenPos.x + kScreenCoordsTileWidthHalf;
        const auto bottom = screenPos.y + kScreenCoordsTileHeightHalf;
        return !(left > viewPos.x + ViewWidth() || top > viewPos.y + ViewHeight() || right < viewPos.x || bottom < viewPos.y);
    }

    [[nodiscard]] ScreenCoordsXY Viewport::ScreenToViewportCoord(const ScreenCoordsXY& screenCoords) const
    {
        ScreenCoordsXY ret;
        ret.x = (zoom.ApplyTo(screenCoords.x - pos.x)) + viewPos.x;
        ret.y = (zoom.ApplyTo(screenCoords.y - pos.y)) + viewPos.y;
        return ret;
    }

    void Viewport::Invalidate() const
    {
        ViewportInvalidate(this, { viewPos, viewPos + ScreenCoordsXY{ ViewWidth(), ViewHeight() } });
    }

    void Viewport::Invalidate(
        const int32_t x, const int32_t y, const int32_t z0, const int32_t z1, const ZoomLevel maxZoom) const
    {
        if ((maxZoom == ZoomLevel{ -1 } || zoom <= ZoomLevel{ maxZoom }))
        {
            const auto screenCoord = Translate3DTo2DWithZ(
                rotation, CoordsXYZ{ x + kCoordsXYHalfTile, y + kCoordsXYHalfTile, 0 });

            const auto topLeft = screenCoord - ScreenCoordsXY(kScreenCoordsTileWidthHalf, kScreenCoordsTileHeight + z1);
            const auto bottomRight = screenCoord + ScreenCoordsXY(kScreenCoordsTileWidthHalf, kScreenCoordsTileHeight - z0);

            ViewportInvalidate(this, ScreenRect{ topLeft, bottomRight });
        }
    }

    CoordsXY ViewportPosToMapPos(const ScreenCoordsXY& coords, int32_t z, uint8_t rotation)
    {
        // Reverse of Translate3DTo2DWithZ
        CoordsXY ret = { coords.y - coords.x / 2 + z, coords.y + coords.x / 2 + z };
        auto inverseRotation = DirectionFlipXAxis(rotation);
        return ret.rotate(inverseRotation);
    }

    /**
     *
     *  rct2: 0x00664689
     */
    void ShowGridlines()
    {
        if (gShowGridLinesRefCount == 0)
        {
            WindowBase* mainWindow = WindowGetMain();
            if (mainWindow != nullptr)
            {
                if (!(mainWindow->viewport->flags & VIEWPORT_FLAG_GRIDLINES))
                {
                    mainWindow->viewport->flags |= VIEWPORT_FLAG_GRIDLINES;
                    mainWindow->invalidate();
                }
            }
        }
        gShowGridLinesRefCount++;
    }

    /**
     *
     *  rct2: 0x006646B4
     */
    void HideGridlines()
    {
        if (gShowGridLinesRefCount > 0)
            gShowGridLinesRefCount--;

        if (gShowGridLinesRefCount == 0)
        {
            WindowBase* mainWindow = WindowGetMain();
            if (mainWindow != nullptr)
            {
                if (!Config::Get().general.alwaysShowGridlines)
                {
                    mainWindow->viewport->flags &= ~VIEWPORT_FLAG_GRIDLINES;
                    mainWindow->invalidate();
                }
            }
        }
    }

    /**
     *
     *  rct2: 0x00664E8E
     */
    void ShowLandRights()
    {
        if (gShowLandRightsRefCount == 0)
        {
            WindowBase* mainWindow = WindowGetMain();
            if (mainWindow != nullptr)
            {
                if (!(mainWindow->viewport->flags & VIEWPORT_FLAG_LAND_OWNERSHIP))
                {
                    mainWindow->viewport->flags |= VIEWPORT_FLAG_LAND_OWNERSHIP;
                    mainWindow->invalidate();
                }
            }
        }
        gShowLandRightsRefCount++;
    }

    /**
     *
     *  rct2: 0x00664EB9
     */
    void HideLandRights()
    {
        if (gShowLandRightsRefCount > 0)
            gShowLandRightsRefCount--;

        if (gShowLandRightsRefCount == 0)
        {
            WindowBase* mainWindow = WindowGetMain();
            if (mainWindow != nullptr)
            {
                if (mainWindow->viewport->flags & VIEWPORT_FLAG_LAND_OWNERSHIP)
                {
                    mainWindow->viewport->flags &= ~VIEWPORT_FLAG_LAND_OWNERSHIP;
                    mainWindow->invalidate();
                }
            }
        }
    }

    /**
     *
     *  rct2: 0x00664EDD
     */
    void ShowConstructionRights()
    {
        if (gShowConstructionRightsRefCount == 0)
        {
            WindowBase* mainWindow = WindowGetMain();
            if (mainWindow != nullptr)
            {
                if (!(mainWindow->viewport->flags & VIEWPORT_FLAG_CONSTRUCTION_RIGHTS))
                {
                    mainWindow->viewport->flags |= VIEWPORT_FLAG_CONSTRUCTION_RIGHTS;
                    mainWindow->invalidate();
                }
            }
        }
        gShowConstructionRightsRefCount++;
    }

    /**
     *
     *  rct2: 0x00664F08
     */
    void HideConstructionRights()
    {
        if (gShowConstructionRightsRefCount > 0)
            gShowConstructionRightsRefCount--;

        if (gShowConstructionRightsRefCount == 0)
        {
            WindowBase* mainWindow = WindowGetMain();
            if (mainWindow != nullptr)
            {
                if (mainWindow->viewport->flags & VIEWPORT_FLAG_CONSTRUCTION_RIGHTS)
                {
                    mainWindow->viewport->flags &= ~VIEWPORT_FLAG_CONSTRUCTION_RIGHTS;
                    mainWindow->invalidate();
                }
            }
        }
    }

    /**
     *
     *  rct2: 0x006CB70A
     */
    void ViewportSetVisibility(ViewportVisibility mode)
    {
        WindowBase* window = WindowGetMain();

        if (window != nullptr)
        {
            Viewport* vp = window->viewport;
            uint32_t invalidate = 0;

            switch (mode)
            {
                case ViewportVisibility::standard:
                { // Set all these flags to 0, and invalidate if any were active
                    uint32_t mask = VIEWPORT_FLAG_UNDERGROUND_INSIDE | VIEWPORT_FLAG_HIDE_RIDES | VIEWPORT_FLAG_HIDE_SCENERY
                        | VIEWPORT_FLAG_HIDE_PATHS | VIEWPORT_FLAG_LAND_HEIGHTS | VIEWPORT_FLAG_TRACK_HEIGHTS
                        | VIEWPORT_FLAG_PATH_HEIGHTS | VIEWPORT_FLAG_HIDE_GUESTS | VIEWPORT_FLAG_HIDE_STAFF
                        | VIEWPORT_FLAG_HIDE_BASE | VIEWPORT_FLAG_HIDE_VERTICAL | VIEWPORT_FLAG_HIDE_VEHICLES
                        | VIEWPORT_FLAG_HIDE_SUPPORTS | VIEWPORT_FLAG_HIDE_VEGETATION;

                    invalidate += vp->flags & mask;
                    vp->flags &= ~mask;
                    break;
                }
                case ViewportVisibility::undergroundViewOn:      // 6CB79D
                case ViewportVisibility::undergroundViewGhostOn: // 6CB7C4
                    // Set underground on, invalidate if it was off
                    invalidate += !(vp->flags & VIEWPORT_FLAG_UNDERGROUND_INSIDE);
                    vp->flags |= VIEWPORT_FLAG_UNDERGROUND_INSIDE;
                    break;
                case ViewportVisibility::trackHeights: // 6CB7EB
                    // Set track heights on, invalidate if off
                    invalidate += !(vp->flags & VIEWPORT_FLAG_TRACK_HEIGHTS);
                    vp->flags |= VIEWPORT_FLAG_TRACK_HEIGHTS;
                    break;
                case ViewportVisibility::undergroundViewOff:      // 6CB7B1
                case ViewportVisibility::undergroundViewGhostOff: // 6CB7D8
                    // Set underground off, invalidate if it was on
                    invalidate += vp->flags & VIEWPORT_FLAG_UNDERGROUND_INSIDE;
                    vp->flags &= ~(static_cast<uint16_t>(VIEWPORT_FLAG_UNDERGROUND_INSIDE));
                    break;
            }
            if (invalidate != 0)
                window->invalidate();
        }
    }

    static bool IsCursorIdVegetation(CursorID cursor)
    {
        switch (cursor)
        {
            case CursorID::treeDown:
            case CursorID::flowerDown:
                return true;
            default:
                return false;
        }
    }

    static bool IsTileElementVegetation(const TileElement* tileElement)
    {
        switch (tileElement->getType())
        {
            case TileElementType::smallScenery:
            {
                auto sceneryItem = tileElement->asSmallScenery();
                auto sceneryEntry = sceneryItem->getEntry();
                if (sceneryEntry != nullptr
                    && (sceneryEntry->flags.has(SmallSceneryFlag::isTree) || IsCursorIdVegetation(sceneryEntry->tool_id)))
                {
                    return true;
                }
                break;
            }
            case TileElementType::largeScenery:
            {
                auto sceneryItem = tileElement->asLargeScenery();
                auto sceneryEntry = sceneryItem->getEntry();
                if (sceneryEntry != nullptr && IsCursorIdVegetation(sceneryEntry->tool_id))
                {
                    return true;
                }
                break;
            }
            case TileElementType::wall:
            {
                auto sceneryItem = tileElement->asWall();
                auto sceneryEntry = sceneryItem->getEntry();
                if (sceneryEntry != nullptr && IsCursorIdVegetation(sceneryEntry->tool_id))
                {
                    return true;
                }
                break;
            }
            default:
                break;
        }
        return false;
    }

    VisibilityKind GetPaintStructVisibility(const PaintStruct* ps, uint32_t viewFlags)
    {
        // the cut-away view is active and see-through is activated
        auto cutAwayViewWithTransparency = (viewFlags & VIEWPORT_FLAG_CLIP_VIEW)
            && (viewFlags & VIEWPORT_FLAG_CLIP_VIEW_SEE_THROUGH);

        // the element is above the cut-off height
        auto clipped = cutAwayViewWithTransparency && ps->Element == nullptr && ps->Entity
            && ps->Entity.z > (gClipHeight * kCoordsZStep);

        // the entity is above the cut-off height
        clipped |= cutAwayViewWithTransparency && ps->Element != nullptr
            && (ps->Element->getBaseZ() > gClipHeight * kCoordsZStep);

        switch (ps->InteractionItem)
        {
            case ViewportInteractionItem::entity:
                if (ps->Entity)
                {
                    switch (ps->Entity.type)
                    {
                        case EntityType::vehicle:
                        {
                            if (viewFlags & VIEWPORT_FLAG_HIDE_VEHICLES || clipped)
                            {
                                return (viewFlags & VIEWPORT_FLAG_INVISIBLE_VEHICLES) ? VisibilityKind::hidden
                                                                                      : VisibilityKind::partial;
                            }
                            // Rides without track can technically have a 'vehicle':
                            // these should be hidden if 'hide rides' is enabled
                            if (viewFlags & VIEWPORT_FLAG_HIDE_RIDES || clipped)
                            {
                                auto vehicle = getGameState().entities.getEntity<Vehicle>(ps->Entity.id);
                                if (vehicle == nullptr)
                                    break;

                                auto ride = vehicle->GetRide();
                                if (ride != nullptr && !ride->getRideTypeDescriptor().flags.has(RtdFlag::hasTrack))
                                {
                                    return (viewFlags & VIEWPORT_FLAG_INVISIBLE_RIDES) ? VisibilityKind::hidden
                                                                                       : VisibilityKind::partial;
                                }
                            }
                            break;
                        }
                        case EntityType::guest:
                            if (viewFlags & VIEWPORT_FLAG_HIDE_GUESTS)
                            {
                                return VisibilityKind::hidden;
                            }
                            else if (clipped)
                            {
                                return VisibilityKind::partial;
                            }
                            break;
                        case EntityType::staff:
                            if (viewFlags & VIEWPORT_FLAG_HIDE_STAFF)
                            {
                                return VisibilityKind::hidden;
                            }
                            else if (clipped)
                            {
                                return VisibilityKind::partial;
                            }
                            break;
                        default:
                            if (clipped)
                            {
                                return VisibilityKind::partial;
                            }
                            break;
                    }
                }
                break;
            case ViewportInteractionItem::ride:
                if (viewFlags & VIEWPORT_FLAG_HIDE_RIDES || clipped)
                {
                    return (viewFlags & VIEWPORT_FLAG_INVISIBLE_RIDES) ? VisibilityKind::hidden : VisibilityKind::partial;
                }
                break;
            case ViewportInteractionItem::footpath:
            case ViewportInteractionItem::pathAddition:
            case ViewportInteractionItem::banner:
                if (viewFlags & VIEWPORT_FLAG_HIDE_PATHS || clipped)
                {
                    return (viewFlags & VIEWPORT_FLAG_INVISIBLE_PATHS) ? VisibilityKind::hidden : VisibilityKind::partial;
                }
                break;
            case ViewportInteractionItem::scenery:
            case ViewportInteractionItem::largeScenery:
            case ViewportInteractionItem::wall:
                if (ps->Element != nullptr)
                {
                    if (IsTileElementVegetation(ps->Element))
                    {
                        if (viewFlags & VIEWPORT_FLAG_HIDE_VEGETATION || clipped)
                        {
                            return (viewFlags & VIEWPORT_FLAG_INVISIBLE_VEGETATION) ? VisibilityKind::hidden
                                                                                    : VisibilityKind::partial;
                        }
                    }
                    else
                    {
                        if (viewFlags & VIEWPORT_FLAG_HIDE_SCENERY || clipped)
                        {
                            return (viewFlags & VIEWPORT_FLAG_INVISIBLE_SCENERY) ? VisibilityKind::hidden
                                                                                 : VisibilityKind::partial;
                        }
                    }
                }
                if (ps->InteractionItem == ViewportInteractionItem::wall
                    && (viewFlags & VIEWPORT_FLAG_UNDERGROUND_INSIDE || clipped))
                {
                    return VisibilityKind::partial;
                }
                break;
            default:
                if (clipped)
                {
                    return VisibilityKind::partial;
                }
                break;
        }
        return VisibilityKind::visible;
    }

    /**
     * Checks if a PaintStruct sprite type is in the filter mask.
     */
    static bool PSInteractionTypeIsInFilter(PaintStruct* ps, ViewportInteractionItems filter)
    {
        return (ps->InteractionItem != ViewportInteractionItem::none && ps->InteractionItem != ViewportInteractionItem::label
                && ps->InteractionItem <= ViewportInteractionItem::banner)
            && filter.has(ps->InteractionItem);
    }

    /**
     * rct2: 0x00679236, 0x00679662, 0x00679B0D, 0x00679FF1
     */
    static bool IsPixelPresentBMP(
        const uint32_t imageType, const G1Element* g1, const int32_t x, const int32_t y, const PaletteMap& paletteMap)
    {
        uint8_t* index = g1->offset + (y * g1->width) + x;

        // Needs investigation as it has no consideration for pure BMP maps.
        if (!g1->flags.has(G1Flag::hasTransparency))
        {
            return false;
        }

        if (imageType & IMAGE_TYPE_REMAP)
        {
            return paletteMap[*index] != PaletteIndex::transparent;
        }

        if (imageType & IMAGE_TYPE_TRANSPARENT)
        {
            return false;
        }

        return (*index != 0);
    }

    /**
     * rct2: 0x0067933B, 0x00679788, 0x00679C4A, 0x0067A117
     */
    static bool IsPixelPresentRLE(const uint8_t* imgData, const int32_t x, const int32_t y)
    {
        uint16_t lineOffset;
        std::memcpy(&lineOffset, &imgData[y * sizeof(uint16_t)], sizeof(uint16_t));
        const uint8_t* data8 = imgData + lineOffset;

        bool lastDataLine = false;
        while (!lastDataLine)
        {
            int32_t numPixels = *data8++;
            uint8_t pixelRunStart = *data8++;
            lastDataLine = numPixels & 0x80;
            numPixels &= 0x7F;
            data8 += numPixels;

            if (pixelRunStart <= x && x < pixelRunStart + numPixels)
                return true;
        }
        return false;
    }

    /**
     * rct2: 0x00679074
     */
    static bool IsSpriteInteractedWithPaletteSet(
        RenderTarget& rt, ImageId imageId, const ScreenCoordsXY& coords, const PaletteMap& paletteMap, const uint8_t imageType)
    {
        PROFILED_FUNCTION();

        const G1Element* g1 = GfxGetG1Element(imageId);
        if (g1 == nullptr)
        {
            return false;
        }

        ZoomLevel zoomLevel = rt.zoom_level;
        ScreenCoordsXY interactionPoint{ rt.WorldX(), rt.WorldY() };
        ScreenCoordsXY origin = coords;

        if (rt.zoom_level > ZoomLevel{ 0 })
        {
            if (g1->flags.has(G1Flag::noZoomDraw))
            {
                return false;
            }

            while (g1->flags.has(G1Flag::hasZoomSprite) && zoomLevel > ZoomLevel{ 0 })
            {
                imageId = imageId.WithIndex(imageId.GetIndex() - g1->zoomedOffset);
                g1 = GfxGetG1Element(imageId);
                if (g1 == nullptr || g1->flags.has(G1Flag::noZoomDraw))
                {
                    return false;
                }
                zoomLevel = zoomLevel - 1;
                interactionPoint.x >>= 1;
                interactionPoint.y >>= 1;
                origin.x >>= 1;
                origin.y >>= 1;
            }
        }

        origin.x += g1->xOffset;
        origin.y += g1->yOffset;
        interactionPoint -= origin;

        if (interactionPoint.x < 0 || interactionPoint.y < 0 || interactionPoint.x >= g1->width
            || interactionPoint.y >= g1->height)
        {
            return false;
        }

        if (g1->flags.has(G1Flag::hasRLECompression))
        {
            return IsPixelPresentRLE(g1->offset, interactionPoint.x, interactionPoint.y);
        }

        if (!g1->flags.has(G1Flag::one))
        {
            return IsPixelPresentBMP(imageType, g1, interactionPoint.x, interactionPoint.y, paletteMap);
        }

        Guard::Assert(false, "Invalid image type encountered.");
        return false;
    }

    /**
     *
     *  rct2: 0x00679023
     */

    static bool IsSpriteInteractedWith(RenderTarget& rt, ImageId imageId, const ScreenCoordsXY& coords)
    {
        PROFILED_FUNCTION();

        auto paletteMap = PaletteMap::GetDefault();
        uint8_t imageType;
        if (imageId.HasPrimary() || imageId.IsRemap())
        {
            imageType = IMAGE_TYPE_REMAP;
            FilterPaletteID filterPaletteId;
            if (imageId.HasSecondary())
            {
                filterPaletteId = static_cast<FilterPaletteID>(imageId.GetPrimary());
            }
            else
            {
                filterPaletteId = static_cast<FilterPaletteID>(imageId.GetRemap());
            }
            if (auto pm = GetPaletteMapForColour(filterPaletteId); pm.has_value())
            {
                paletteMap = pm.value();
            }
        }
        else
        {
            imageType = IMAGE_TYPE_DEFAULT;
        }
        return IsSpriteInteractedWithPaletteSet(rt, imageId, coords, paletteMap, imageType);
    }

    const std::list<Viewport>& GetAllViewports()
    {
        return _viewports;
    }

    /**
     *
     *  rct2: 0x0068862C
     */
    InteractionInfo SetInteractionInfoFromPaintSession(
        PaintSession* session, uint32_t viewFlags, ViewportInteractionItems filter)
    {
        PROFILED_FUNCTION();

        InteractionInfo info{};

        PaintStruct* ps = session->PaintHead;
        while (ps != nullptr)
        {
            PaintStruct* old_ps = ps;
            PaintStruct* next_ps = ps;
            while (next_ps != nullptr)
            {
                ps = next_ps;
                if (IsSpriteInteractedWith(session->rt, ps->image_id, ps->ScreenPos))
                {
                    if (PSInteractionTypeIsInFilter(ps, filter)
                        && GetPaintStructVisibility(ps, viewFlags) == VisibilityKind::visible)
                    {
                        info = { ps };
                    }
                }
                next_ps = ps->Children;
            }

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wnull-dereference"
            for (AttachedPaintStruct* attached_ps = ps->Attached; attached_ps != nullptr; attached_ps = attached_ps->NextEntry)
            {
                if (IsSpriteInteractedWith(session->rt, attached_ps->image_id, ps->ScreenPos + attached_ps->RelativePos))
                {
                    if (PSInteractionTypeIsInFilter(ps, filter)
                        && GetPaintStructVisibility(ps, viewFlags) == VisibilityKind::visible)
                    {
                        info = { ps };
                    }
                }
            }
#pragma GCC diagnostic pop

            ps = old_ps->NextQuadrantEntry;
        }
        return info;
    }

    /**
     *
     *  rct2: 0x00685ADC
     * screenX: eax
     * screenY: ebx
     * flags: edx
     * x: ax
     * y: cx
     * interactionType: bl
     * tileElement: edx
     * viewport: edi
     */
    InteractionInfo GetMapCoordinatesFromPos(const ScreenCoordsXY& screenCoords, ViewportInteractionItems flags)
    {
        auto* windowMgr = Ui::GetWindowManager();
        WindowBase* window = windowMgr->FindFromPoint(screenCoords);
        return GetMapCoordinatesFromPosWindow(window, screenCoords, flags);
    }

    InteractionInfo GetMapCoordinatesFromPosWindow(
        WindowBase* window, const ScreenCoordsXY& screenCoords, ViewportInteractionItems flags)
    {
        InteractionInfo info{};
        if (window == nullptr || window->viewport == nullptr)
        {
            return info;
        }

        Viewport* viewport = window->viewport;
        auto viewLoc = screenCoords;
        viewLoc -= viewport->pos;
        if ((viewLoc.x >= 0) && (viewLoc.x < viewport->width) && (viewLoc.y >= 0) && (viewLoc.y < viewport->height))
        {
            viewLoc.x = viewport->zoom.ApplyTo(viewLoc.x);
            viewLoc.y = viewport->zoom.ApplyTo(viewLoc.y);
            viewLoc += viewport->viewPos;
            if (viewport->zoom > ZoomLevel{ 0 })
            {
                viewLoc.x &= viewport->zoom.ApplyTo(0xFFFFFFFF) & 0xFFFFFFFF;
                viewLoc.y &= viewport->zoom.ApplyTo(0xFFFFFFFF) & 0xFFFFFFFF;
            }
            RenderTarget rt;
            rt.zoom_level = viewport->zoom;
            rt.x = viewport->zoom.ApplyInversedTo(viewLoc.x);
            rt.y = viewport->zoom.ApplyInversedTo(viewLoc.y);
            rt.height = 1;
            rt.width = 1;

            rt.cullingX = rt.x;
            rt.cullingY = rt.y;
            rt.cullingWidth = rt.width;
            rt.cullingHeight = rt.height;

            PaintSession* session = PaintSessionAlloc(rt, viewport->flags, viewport->rotation);
            PaintSessionGenerate(*session);
            PaintSessionArrange(*session);
            info = SetInteractionInfoFromPaintSession(session, viewport->flags, flags);
            PaintSessionFree(session);
        }
        return info;
    }

    /**
     * screenRect represents 2D map coordinates at zoom 0.
     */
    void ViewportInvalidate(const Viewport* viewport, const ScreenRect& screenRect)
    {
        PROFILED_FUNCTION();

        auto zoom = viewport->zoom;
        auto viewPos = viewport->viewPos;
        auto viewportScreenPos = viewport->pos;

        ScreenRect invalidRect = { { zoom.ApplyInversedTo(screenRect.getLeft() - viewPos.x),
                                     zoom.ApplyInversedTo(screenRect.getTop() - viewPos.y) },
                                   { zoom.ApplyInversedTo(screenRect.getRight() - viewPos.x),
                                     zoom.ApplyInversedTo(screenRect.getBottom() - viewPos.y) } };

        if (invalidRect.getTop() >= viewport->height || invalidRect.getBottom() <= 0 || invalidRect.getLeft() >= viewport->width
            || invalidRect.getRight() <= 0)
        {
            return;
        }
        invalidRect.point1 += viewportScreenPos;
        invalidRect.point2 += viewportScreenPos;
        GfxSetDirtyBlocks(invalidRect);
    }

    Viewport* ViewportFindFromPoint(const ScreenCoordsXY& screenCoords)
    {
        auto* windowMgr = Ui::GetWindowManager();
        WindowBase* w = windowMgr->FindFromPoint(screenCoords);
        if (w == nullptr)
            return nullptr;

        Viewport* viewport = w->viewport;
        if (viewport == nullptr)
            return nullptr;

        if (viewport->ContainsScreen(screenCoords))
            return viewport;

        return nullptr;
    }

    /**
     *
     *  rct2: 0x00688972
     * In:
     *      screen_x: eax
     *      screen_y: ebx
     * Out:
     *      x: ax
     *      y: bx
     *      tile_element: edx ?
     *      viewport: edi
     */
    std::optional<CoordsXY> ScreenGetMapXY(const ScreenCoordsXY& screenCoords, Viewport** viewport)
    {
        auto* windowMgr = Ui::GetWindowManager();

        // This will get the tile location but we will need the more accuracy
        WindowBase* window = windowMgr->FindFromPoint(screenCoords);
        if (window == nullptr || window->viewport == nullptr)
        {
            return std::nullopt;
        }
        auto myViewport = window->viewport;
        auto info = GetMapCoordinatesFromPosWindow(window, screenCoords, ViewportInteractionItem::terrain);
        if (info.interactionType == ViewportInteractionItem::none)
        {
            return std::nullopt;
        }

        auto start_vp_pos = myViewport->ScreenToViewportCoord(screenCoords);
        CoordsXY cursorMapPos = info.Loc.toTileCentre();

        // Iterates the cursor location to work out exactly where on the tile it is
        for (int32_t i = 0; i < 5; i++)
        {
            int32_t z = TileElementHeight(cursorMapPos);
            cursorMapPos = ViewportPosToMapPos(start_vp_pos, z, myViewport->rotation);
            cursorMapPos.x = std::clamp(cursorMapPos.x, info.Loc.x, info.Loc.x + 31);
            cursorMapPos.y = std::clamp(cursorMapPos.y, info.Loc.y, info.Loc.y + 31);
        }

        if (viewport != nullptr)
            *viewport = myViewport;

        return cursorMapPos;
    }

    /**
     *
     *  rct2: 0x006894D4
     */
    std::optional<CoordsXY> ScreenGetMapXYWithZ(const ScreenCoordsXY& screenCoords, int32_t z)
    {
        Viewport* viewport = ViewportFindFromPoint(screenCoords);
        if (viewport == nullptr)
        {
            return std::nullopt;
        }

        auto vpCoords = viewport->ScreenToViewportCoord(screenCoords);
        auto mapPosition = ViewportPosToMapPos(vpCoords, z, viewport->rotation);
        if (!MapIsLocationValid(mapPosition))
        {
            return std::nullopt;
        }

        return mapPosition;
    }

    /**
     *
     *  rct2: 0x00689604
     */
    std::optional<CoordsXY> ScreenGetMapXYQuadrant(const ScreenCoordsXY& screenCoords, uint8_t* quadrant)
    {
        auto mapCoords = ScreenGetMapXY(screenCoords, nullptr);
        if (!mapCoords.has_value())
            return std::nullopt;

        *quadrant = MapGetTileQuadrant(*mapCoords);
        return mapCoords->toTileStart();
    }

    /**
     *
     *  rct2: 0x0068964B
     */
    std::optional<CoordsXY> ScreenGetMapXYQuadrantWithZ(const ScreenCoordsXY& screenCoords, int32_t z, uint8_t* quadrant)
    {
        auto mapCoords = ScreenGetMapXYWithZ(screenCoords, z);
        if (!mapCoords.has_value())
            return std::nullopt;

        *quadrant = MapGetTileQuadrant(*mapCoords);
        return mapCoords->toTileStart();
    }

    /**
     *
     *  rct2: 0x00689692
     */
    std::optional<CoordsXY> ScreenGetMapXYSide(const ScreenCoordsXY& screenCoords, uint8_t* side)
    {
        auto mapCoords = ScreenGetMapXY(screenCoords, nullptr);
        if (!mapCoords.has_value())
            return std::nullopt;

        *side = MapGetTileSide(*mapCoords);
        return mapCoords->toTileStart();
    }

    /**
     *
     *  rct2: 0x006896DC
     */
    std::optional<CoordsXY> ScreenGetMapXYSideWithZ(const ScreenCoordsXY& screenCoords, int32_t z, uint8_t* side)
    {
        auto mapCoords = ScreenGetMapXYWithZ(screenCoords, z);
        if (!mapCoords.has_value())
            return std::nullopt;

        *side = MapGetTileSide(*mapCoords);
        return mapCoords->toTileStart();
    }

    ScreenCoordsXY Translate3DTo2DWithZ(int32_t rotation, const CoordsXYZ& pos)
    {
        auto rotated = pos.rotate(rotation);
        // Use right shift to avoid issues like #9301
        return ScreenCoordsXY{ rotated.y - rotated.x, ((rotated.x + rotated.y) >> 1) - pos.z };
    }

    /**
     * Get current viewport rotation.
     *
     * If an invalid rotation is detected and DEBUG_LEVEL_1 is enabled, an error
     * will be reported.
     *
     * @returns rotation in range 0-3 (inclusive)
     */
    uint8_t GetCurrentRotation()
    {
        auto* viewport = ViewportGetMain();
        if (viewport == nullptr)
        {
            LOG_VERBOSE("No viewport found! Will return 0.");
            return 0;
        }
        uint8_t rotation = viewport->rotation;
        uint8_t rotation_masked = rotation & 3;
#if defined(DEBUG_LEVEL_1) && DEBUG_LEVEL_1
        if (rotation != rotation_masked)
        {
            LOG_ERROR(
                "Found wrong rotation %d! Will return %d instead.", static_cast<uint32_t>(rotation),
                static_cast<uint32_t>(rotation_masked));
        }
#endif // DEBUG_LEVEL_1
        return rotation_masked;
    }

    int32_t GetHeightMarkerOffset()
    {
        // Height labels in units
        if (Config::Get().general.showHeightAsUnits)
            return 0;

        // Height labels in feet
        if (Config::Get().general.measurementFormat == MeasurementFormat::imperial)
            return 1 * 256;

        // Height labels in metres
        return 2 * 256;
    }

    void ViewportSetSavedView()
    {
        WindowBase* w = WindowGetMain();
        if (w != nullptr)
        {
            Viewport* viewport = w->viewport;
            auto& gameState = getGameState();

            gameState.savedView = ScreenCoordsXY{ viewport->ViewWidth() / 2, viewport->ViewHeight() / 2 } + viewport->viewPos;

            gameState.savedViewZoom = viewport->zoom;
            gameState.savedViewRotation = viewport->rotation;
        }
    }

    ViewportList GetVisibleViewports() noexcept
    {
        ViewportList viewports;
        for (auto& viewport : _viewports)
        {
            if (viewport.isVisible)
            {
                viewports.push_back(&viewport);
            }
        };
        return viewports;
    }
} // namespace OpenRCT2
