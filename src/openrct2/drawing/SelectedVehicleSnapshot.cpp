// Copyright (c) 2014-2026 OpenRCT2 developers. GPL-3.0-or-later.
#include "SelectedVehicleSnapshot.h"
#include "../GameState.h"
#include "../Limits.h"
#include "../interface/Viewport.h"
#include "../paint/Paint.h"
#include "../paint/entity/Paint.Vehicle.h"
#include "../ride/Vehicle.h"
#include "../ride/TrackDesign.h"
#include "../world/Map.h"
#include "../world/MapPresentationSnapshot.h"
#include "../world/WorldObjectPresentation.h"
#include "FilterPaletteIds.h"
#include "RenderTarget.h"
#include <algorithm>
#include <stdexcept>
#include <unordered_set>

namespace OpenRCT2::Drawing
{
    namespace
    {
        constexpr size_t kMaximumComponents = 16384;
        ImageId VisibleImage(const PaintStruct& ps, ImageId image, uint32_t flags)
        {
            switch (GetPaintStructVisibility(&ps, flags))
            {
                case VisibilityKind::partial: return image.WithTransparency(FilterPaletteID::paletteDarken1);
                case VisibilityKind::hidden: return {};
                default: return image;
            }
        }
        std::array<int32_t, 6> Bounds(const PaintStruct& ps)
        {
            return { ps.Bounds.x, ps.Bounds.y, ps.Bounds.z, ps.Bounds.x_end, ps.Bounds.y_end, ps.Bounds.z_end };
        }
        uint32_t Append(SelectedVehicleView& view, SelectedVehicleComponent component)
        {
            if (view.components.size() == kMaximumComponents)
                throw std::length_error("Selected train exceeds the bounded auxiliary component budget");
            const auto index = static_cast<uint32_t>(view.components.size());
            view.components.push_back(component);
            return index;
        }
        void CaptureParent(SelectedVehicleView& view, const PaintStruct& root, uint32_t car)
        {
            const PaintStruct* ps = &root;
            uint32_t predecessor = static_cast<uint32_t>(view.components.size());
            bool parent = true;
            for (size_t depth = 0; ps != nullptr; ++depth)
            {
                if (depth >= kMaximumComponents)
                    throw std::logic_error("Selected vehicle child chain is cyclic");
                const auto index = Append(view, {
                    .bounds = Bounds(*ps), .screen = ps->ScreenPos, .originalImage = ps->image_id,
                    .image = VisibleImage(*ps, ps->image_id, view.request.viewFlags), .parent = predecessor, .car = car,
                    .relation = parent ? SelectedVehicleRelation::parent : SelectedVehicleRelation::child });
                if (ps->Children != nullptr)
                {
                    predecessor = index;
                    parent = false;
                    ps = ps->Children;
                    continue;
                }
                // Original drawing visits attachments only on the terminal child, in linked-list order.
                size_t attachments = 0;
                for (const auto* attached = ps->Attached; attached != nullptr; attached = attached->NextEntry)
                {
                    if (++attachments > kMaximumComponents)
                        throw std::logic_error("Selected vehicle attachment chain is cyclic");
                    Append(view, { .bounds = Bounds(*ps), .screen = ps->ScreenPos + attached->RelativePos,
                        .originalImage = attached->image_id,
                        .image = VisibleImage(*ps, attached->image_id, view.request.viewFlags),
                        .maskImage = attached->ColourImageId, .parent = index, .car = car,
                        .relation = SelectedVehicleRelation::attached, .masked = attached->IsMasked });
                }
                break;
            }
        }
        bool ClipAllows(const Vehicle& vehicle, const SelectedVehicleRequest& request)
        {
            if ((request.viewFlags & VIEWPORT_FLAG_CLIP_VIEW) == 0)
                return true;
            return ((request.viewFlags & VIEWPORT_FLAG_CLIP_VIEW_SEE_THROUGH) != 0
                    || vehicle.z <= request.clipHeight * kCoordsZStep)
                && vehicle.x >= request.clipFirst.x && vehicle.x <= request.clipLast.x + 31
                && vehicle.y >= request.clipFirst.y && vehicle.y <= request.clipLast.y + 31;
        }
    }

    std::shared_ptr<const SelectedVehicleSnapshot> CaptureSelectedVehicleSnapshot(
        std::span<const SelectedVehicleRequest> requests)
    {
        if (requests.empty())
            return {};
        if (requests.size() > 64)
            throw std::length_error("Too many selected-vehicle auxiliary views");
        auto& state = getGameState();
        auto result = std::make_shared<SelectedVehicleSnapshot>();
        result->worldEpoch = GetMapPresentationEpoch();
        result->entityEpoch = state.entities.GetEntityVisualEpoch();
        result->objectRevision = GetWorldObjectRevision();
        result->sourceTick = state.currentTicks;
        for (const auto& request : requests)
        {
            if (request.rotation > 3 || request.zoom < -2 || request.zoom > 3)
                throw std::invalid_argument("Invalid selected-vehicle camera");
            auto& view = result->views.emplace_back();
            view.request = request;
            if (request.zoom > 2 || gTrackDesignSaveMode
                || (request.viewFlags & (VIEWPORT_FLAG_HIDE_ENTITIES | VIEWPORT_FLAG_HIGHLIGHT_PATH_ISSUES)) != 0
                || request.entity.id.IsNull() || request.entity.id.ToUnderlying() >= kMaxEntities
                || state.entities.GetEntityVisualHandle(request.entity.id) != request.entity)
                continue;
            // Entity buckets are originally traversed in stable EntityId order, independently of train linkage.
            std::vector<Vehicle*> cars;
            auto id = request.entity.id;
            for (size_t count = 0; count < Limits::kMaxCarsPerTrain && !id.IsNull(); ++count)
            {
                auto* vehicle = state.entities.getEntity<Vehicle>(id);
                if (vehicle == nullptr)
                {
                    id = EntityId::GetNull();
                    break;
                }
                if (std::find(cars.begin(), cars.end(), vehicle) != cars.end())
                    throw std::logic_error("Selected vehicle train linkage is cyclic");
                cars.push_back(vehicle);
                id = vehicle->next_vehicle_on_train;
            }
            if (!id.IsNull())
                throw std::length_error("Selected vehicle train exceeds the bounded car budget");
            std::sort(cars.begin(), cars.end(), [](const auto* a, const auto* b) { return a->id < b->id; });
            for (const auto* vehicle : cars)
            {
                if (vehicle->x == kLocationNull || !MapIsLocationValid(vehicle->getLocation()) || !ClipAllows(*vehicle, request))
                    continue;
                // No target pixels are allocated or touched. Parent allocation culling is deferred to GPU paint columns.
                RenderTarget target{ .x = -1048576, .y = -1048576, .width = 2097152, .height = 2097152,
                    .cullingX = -1048576, .cullingY = -1048576, .cullingWidth = 2097152, .cullingHeight = 2097152,
                    .zoom_level = ZoomLevel{ request.zoom } };
                auto* session = PaintSessionAlloc(target, request.viewFlags, request.rotation);
                struct Release { PaintSession* session; ~Release() { PaintSessionFree(session); } } release{ session };
                session->CurrentlyDrawnEntity = vehicle;
                session->SpritePosition = { vehicle->x, vehicle->y };
                session->MapPosition = CoordsXY{ vehicle->x, vehicle->y }.toTileStart();
                session->InteractionType = ViewportInteractionItem::entity;
                PaintVehicle(*session, *vehicle, ((request.rotation << 3) + vehicle->orientation) & 31);
                const auto screen = Translate3DTo2DWithZ(request.rotation, vehicle->getLocation());
                const auto carIndex = static_cast<uint32_t>(view.cars.size());
                auto& car = view.cars.emplace_back(SelectedVehicleCar{
                    .entity = state.entities.GetEntityVisualHandle(vehicle->id),
                    .tile = { vehicle->x / 32, vehicle->y / 32 },
                    .coarseCull = { screen - ScreenCoordsXY{ vehicle->spriteData.width, vehicle->spriteData.heightMin },
                                    screen + ScreenCoordsXY{ vehicle->spriteData.width, vehicle->spriteData.heightMax } },
                    .first = static_cast<uint32_t>(view.components.size()) });
                std::unordered_set<const void*> roots;
                for (const auto quadrant : session->ActiveQuadrants)
                    for (auto* ps = session->Quadrants[quadrant]; ps != nullptr; ps = ps->NextQuadrantEntry)
                        roots.insert(ps);
                // This is allocation order, not a CPU graphical sort. The GPU performs quadrant prepend/arrangement.
                const auto capture = [&](const auto& storage) {
                    for (const auto& entry : storage)
                        if (roots.contains(&entry))
                            CaptureParent(view, *reinterpret_cast<const PaintStruct*>(&entry), carIndex);
                };
                capture(session->paintEntries.fixedPaintEntries);
                if (session->paintEntries.dynamicPaintEntries)
                    capture(*session->paintEntries.dynamicPaintEntries);
                car.count = static_cast<uint32_t>(view.components.size()) - car.first;
            }
        }
        return result;
    }
}
