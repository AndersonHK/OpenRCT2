// Copyright (c) 2014-2026 OpenRCT2 developers. GPL-3.0-or-later.
#pragma once
#include "RetainedTerrain.h"

#include <array>
#include <vector>

namespace OpenRCT2::Ui::Gpu
{
    class AtlasAssetLease;
}

namespace OpenRCT2::Ui::Gpu::Terrain
{
    constexpr uint32_t kDrawMaximumColumns = 241;
    constexpr uint32_t kDrawColumnCapacity = 8192;
    constexpr uint32_t kDrawSpriteCapacity = 262144;

    struct DrawSpriteVariant
    {
        int32_t width{}, height{}, xOffset{}, yOffset{};
        uint32_t asset{}, flags{}; // bit0 RLE; bit1 suppresses raster while retaining original parent culling.
        int32_t effectiveZoom{}, coordinateShift{};
    };
    struct DrawSpriteMetadata
    {
        uint32_t imageIndex{};
        int32_t width{}, height{}, xOffset{}, yOffset{}; // Original painter-cull metadata.
        uint32_t flags{}, reserved0{}, reserved1{};      // Must be zero for this bounded path.
        std::array<DrawSpriteVariant, 2> variants;
    };
    struct DrawSpriteTable
    {
        uint64_t revision{};
        // Strictly increasing imageIndex. Atlas generation is part of revision.
        std::vector<DrawSpriteMetadata> records;
        std::shared_ptr<const AtlasAssetLease> residency;
    };
    struct DrawCamera
    {
        int32_t x{}, y{}, width{}, height{}; // Logical world-plane target, not world tile coordinates.
        int32_t clipX{}, clipY{};            // Framebuffer offset of target.
        uint32_t rotation{};
        int32_t zoom{};
        uint32_t transparent{}, stableSort{};
        int32_t depthBase{};
        // Bounded publication budgets; storage strides remain fixed. Zero is a
        // deliberate no-capacity diagnostic, never an unbounded sentinel.
        uint32_t parentCapacity = kDrawColumnCapacity;
        uint32_t commandCapacity = kDrawColumnCapacity;
        float entityInterpolation = 1.0f;
        uint32_t sourceTick{};
    };
    // Debug-visible GPU painter parent; no CPU producer of these records.
    struct DrawParent
    {
        int32_t x{}, y{}, z{}, xEnd{}, yEnd{}, zEnd{}, screenX{}, screenY{};
        uint32_t primitive{}, sprite{};
        int32_t next{};
        uint32_t quadrant{}, flags{}, attachedFirst{}, attachedCount{}, reserved{};
    };
    struct DrawColumnStatus
    {
        uint32_t error{}, parentCount{}, commandCount{};
        int32_t head{};
        // VkDrawIndirectCommand at byte16. firstInstance is always zero; each
        // column binds its own fixed vertex-buffer range without optional features.
        uint32_t vertexCount{}, instanceCount{}, firstVertex{}, firstInstance{};
    };
    static_assert(sizeof(DrawSpriteVariant) == 32);
    static_assert(sizeof(DrawSpriteMetadata) == 96 && offsetof(DrawSpriteMetadata, variants) == 32);
    static_assert(sizeof(DrawParent) == 64 && offsetof(DrawParent, next) == 40);
    static_assert(sizeof(DrawColumnStatus) == 32 && offsetof(DrawColumnStatus, vertexCount) == 16);
} // namespace OpenRCT2::Ui::Gpu::Terrain
