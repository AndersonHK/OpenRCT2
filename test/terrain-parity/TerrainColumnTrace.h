// Copyright (c) 2014-2026 OpenRCT2 developers. GPL-3.0-or-later.
#pragma once

// Diagnostic oracle only. Calls the linked core's unmodified generation and
// arrangement; production Vulkan must never upload these generated lists.
#include <algorithm>
#include <limits>
#include <map>
#include <memory>
#include <openrct2/core/Json.hpp>
#include <openrct2/Context.h>
#include <openrct2/config/Config.h>
#include <openrct2/drawing/Drawing.Sprite.h>
#include <openrct2/drawing/X8DrawingEngine.h>
#include <openrct2/interface/Viewport.h>
#include <openrct2/paint/Paint.h>
#include <set>
#include <stdexcept>
#include <unordered_map>

namespace TerrainColumnTrace
{
    inline std::string Hex(const std::vector<OpenRCT2::Drawing::PaletteIndex>& pixels)
    {
        constexpr char digits[] = "0123456789abcdef";
        std::string result;
        result.reserve(pixels.size() * 2);
        for (const auto pixel : pixels)
        {
            const auto byte = static_cast<uint8_t>(pixel);
            result.push_back(digits[byte >> 4]); result.push_back(digits[byte & 15]);
        }
        return result;
    }

    inline int32_t FloorColumn(int32_t value, int32_t width)
    {
        const int32_t remainder = value % width;
        return value - remainder - (remainder < 0 ? width : 0);
    }

    inline json_t Image(ImageId image)
    {
        return { { "index", image.GetIndex() }, { "primary", static_cast<uint8_t>(image.GetPrimary()) },
                 { "secondary", static_cast<uint8_t>(image.GetSecondary()) },
                 { "tertiary", static_cast<uint8_t>(image.GetTertiary()) },
                 { "hasPrimary", image.HasPrimary() }, { "hasSecondary", image.HasSecondary() },
                 { "remap", image.IsRemap() }, { "blend", image.IsBlended() } };
    }

    inline void RecordSprite(std::map<uint32_t, json_t>& metadata, uint32_t image, uint32_t remaining = 1)
    {
        const auto* element = GfxGetG1Element(image);
        if (element == nullptr || element->offset == nullptr)
            throw std::runtime_error("Terrain trace encountered absent image metadata");
        if (element->width < 1 || element->width > 2048 || element->height < 1 || element->height > 2048
            || element->flags.has(OpenRCT2::G1Flag::isPalette) || element->flags.has(OpenRCT2::G1Flag::noZoomDraw))
            throw std::runtime_error("Terrain trace encountered an unsupported asset");
        metadata.emplace(image, json_t{ { "image", image }, { "width", element->width }, { "height", element->height },
            { "xOffset", element->xOffset }, { "yOffset", element->yOffset }, { "flags", element->flags.holder },
            { "zoomedOffset", element->zoomedOffset } });
        if (remaining != 0 && element->flags.has(OpenRCT2::G1Flag::hasZoomSprite))
        {
            const int64_t next = static_cast<int64_t>(image) - element->zoomedOffset;
            if (next < 0 || next > std::numeric_limits<uint32_t>::max())
                throw std::runtime_error("Terrain trace linked zoom index is invalid");
            RecordSprite(metadata, static_cast<uint32_t>(next), remaining - 1);
        }
    }

    inline json_t Capture(OpenRCT2::Drawing::RenderTarget world, uint8_t rotation, bool transparent, bool stable)
    {
        if (OpenRCT2::Config::Get().general.landscapeSmoothing)
            throw std::invalid_argument("Bounded terrain trace requires explicitly disabled landscape smoothing");
        if (rotation > 3 || world.zoom_level < ZoomLevel{ 0 } || world.zoom_level > ZoomLevel{ 1 }
            || world.width <= 0 || world.width > 1024 || world.height <= 0 || world.height > 1024
            || world.x < -1048576 || world.x > 1048576 || world.y < -1048576 || world.y > 1048576)
            throw std::invalid_argument("Terrain trace camera is outside the bounded contract");
        struct RestoreSort
        {
            bool saved = gPaintStableSort;
            ~RestoreSort() { gPaintStableSort = saved; }
        } restore;
        gPaintStableSort = stable;
        OpenRCT2::Drawing::X8DrawingEngine engine(OpenRCT2::GetContext()->GetUiContext());
        engine.BeginDraw();
        struct EndDrawing
        {
            OpenRCT2::Drawing::X8DrawingEngine& engine;
            ~EndDrawing() { engine.EndDraw(); }
        } endDrawing{ engine };
        world.DrawingEngine = &engine;
        std::vector<OpenRCT2::Drawing::PaletteIndex> pixels(
            static_cast<size_t>(world.width) * world.height, OpenRCT2::Drawing::PaletteIndex::transparent);
        const uint32_t flags = transparent ? static_cast<uint32_t>(OpenRCT2::VIEWPORT_FLAG_TRANSPARENT_BACKGROUND) : 0u;
        const int32_t columnWidth = world.zoom_level.ApplyInversedTo(32);
        const int32_t right = world.x + world.width;
        json_t columns = json_t::array();
        std::map<uint32_t, json_t> spriteMetadata;
        for (int32_t x = FloorColumn(world.x, columnWidth); x < right; x += columnWidth)
        {
            auto target = world;
            target.x = std::max(world.x, x);
            target.width = std::min(right, x + columnWidth) - target.x;
            target.bits = pixels.data() + target.x - world.x;
            target.pitch = world.width - target.width;
            target.cullingX = x;
            target.cullingWidth = columnWidth;
            constexpr int32_t cullingY = ZoomLevel::max().ApplyInversedTo(std::numeric_limits<int32_t>::max()) / 2;
            target.cullingY = -cullingY;
            target.cullingHeight = cullingY * 2;
            std::unique_ptr<PaintSession, void (*)(PaintSession*)> session(
                PaintSessionAlloc(target, flags, rotation), PaintSessionFree);
            if (!session) throw std::runtime_error("Terrain trace could not allocate a paint session");
            PaintSessionGenerate(*session);
            json_t parents = json_t::array();
            std::unordered_map<const PaintStruct*, uint32_t> identities;
            for (int32_t quadrant = 0; quadrant < MaxPaintQuadrants; quadrant++)
            {
                for (const auto* parent = session->Quadrants[quadrant]; parent; parent = parent->NextQuadrantEntry)
                {
                    if (identities.size() >= 65536 || identities.contains(parent) || parent->Children != nullptr)
                        throw std::runtime_error("Terrain trace parent list exceeds scope or contains a cycle");
                    const auto id = static_cast<uint32_t>(identities.size());
                    identities.emplace(parent, id);
                    if (parent->image_id.HasPrimary() || parent->image_id.HasSecondary()
                        || parent->image_id.IsRemap() || parent->image_id.IsBlended())
                        throw std::runtime_error("Recoloured terrain parents are outside the bounded trace/draw scope");
                    RecordSprite(spriteMetadata, parent->image_id.GetIndex());
                    json_t attached = json_t::array();
                    std::set<const AttachedPaintStruct*> seen;
                    for (const auto* entry = parent->Attached; entry; entry = entry->NextEntry)
                    {
                        if (entry->IsMasked)
                            throw std::runtime_error("Masked terrain attachments are outside the bounded trace/draw scope");
                        if (entry->image_id.HasPrimary() || entry->image_id.HasSecondary()
                            || entry->image_id.IsRemap() || entry->image_id.IsBlended())
                            throw std::runtime_error("Recoloured terrain attachments are outside the bounded trace/draw scope");
                        if (!seen.insert(entry).second || seen.size() > 16)
                            throw std::runtime_error("Terrain trace attachment list exceeds scope or contains a cycle");
                        RecordSprite(spriteMetadata, entry->image_id.GetIndex());
                        attached.push_back({ { "image", Image(entry->image_id) }, { "x", entry->RelativePos.x },
                            { "y", entry->RelativePos.y }, { "masked", entry->IsMasked },
                            { "colourImage", Image(entry->ColourImageId) } });
                    }
                    const auto& b = parent->Bounds;
                    parents.push_back({ { "id", id }, { "quadrant", parent->QuadrantIndex },
                        { "mapPosition", { parent->MapPos.x, parent->MapPos.y } },
                        { "image", Image(parent->image_id) }, { "screen", { parent->ScreenPos.x, parent->ScreenPos.y } },
                        { "bounds", { b.x, b.y, b.z, b.x_end, b.y_end, b.z_end } }, { "attached", attached } });
                }
            }
            PaintSessionArrange(*session);
            json_t arranged = json_t::array();
            std::set<const PaintStruct*> visited;
            for (const auto* parent = session->PaintHead; parent; parent = parent->NextQuadrantEntry)
            {
                if (!visited.insert(parent).second || !identities.contains(parent))
                    throw std::runtime_error("Terrain trace arranged list is cyclic or contains unknown parents");
                arranged.push_back(identities.at(parent));
            }
            if (visited.size() != identities.size())
                throw std::runtime_error("Terrain trace arrangement lost a parent");
            // Actual linked-core software rasterization, never new GPU geometry.
            PaintDrawStructs(*session);
            columns.push_back({ { "alignedX", x }, { "target", { target.x, target.y, target.width, target.height } },
                { "culling", { target.cullingX, target.cullingY, target.cullingWidth, target.cullingHeight } },
                { "parentsBeforeArrange", parents }, { "arrangedParentIds", arranged } });
        }
        json_t sprites = json_t::array();
        for (const auto& [image, metadata] : spriteMetadata)
        {
            const auto* element = GfxGetG1Element(image);
            const auto size = static_cast<size_t>(element->width) * element->height;
            std::vector<OpenRCT2::Drawing::PaletteIndex> zero(size, static_cast<OpenRCT2::Drawing::PaletteIndex>(0));
            std::vector<OpenRCT2::Drawing::PaletteIndex> other(size, static_cast<OpenRCT2::Drawing::PaletteIndex>(255));
            OpenRCT2::Drawing::RenderTarget assetTarget{};
            assetTarget.width = element->width; assetTarget.height = element->height;
            assetTarget.bits = zero.data();
            engine.GetDrawingContext()->DrawSprite(assetTarget, ImageId(image), -element->xOffset, -element->yOffset);
            assetTarget.bits = other.data();
            engine.GetDrawingContext()->DrawSprite(assetTarget, ImageId(image), -element->xOffset, -element->yOffset);
            uint32_t coveredZero = 0;
            for (size_t i = 0; i < size; i++)
                if (zero[i] == static_cast<OpenRCT2::Drawing::PaletteIndex>(0)
                    && other[i] == static_cast<OpenRCT2::Drawing::PaletteIndex>(0)) coveredZero++;
            auto decoded = metadata;
            decoded["decodedIndexedHex"] = Hex(zero);
            decoded["coveredZeroPixels"] = coveredZero;
            sprites.push_back(std::move(decoded));
        }
        return { { "schema", 2 }, { "scope", "Linked-core diagnostic order and software indexed raster; no Vulkan pixel proof" },
            { "worldTarget", { world.x, world.y, world.width, world.height } },
            { "zoom", static_cast<int8_t>(world.zoom_level) }, { "rotation", rotation },
            { "transparent", transparent }, { "stableSort", stable }, { "columns", columns },
            { "landscapeSmoothing", false },
            { "clearIndex", 0 }, { "indexedHex", Hex(pixels) },
            { "observedSpriteMetadata", sprites }, { "metadataCoverage", "Only observed images and one linked zoom step" } };
    }
}
