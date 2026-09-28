/*****************************************************************************
 * Copyright (c) 2014-2026 OpenRCT2 developers. GPL-3.0-or-later.
 *****************************************************************************/
#pragma once

#include "TerrainSurfaceRules.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <type_traits>
#include <vector>

namespace OpenRCT2::Ui::Gpu::Terrain
{
    // Bounded foundation. This is NOT a complete-scene admission predicate.
    constexpr uint32_t kRetainedWidth = 32;
    constexpr uint32_t kRetainedTileCount = kRetainedWidth * kRetainedWidth;
    constexpr uint32_t kRetainedChunkSize = 256;
    constexpr uint32_t kRetainedChunkCount = kRetainedTileCount / kRetainedChunkSize;
    constexpr uint32_t kRetainedMaterialCapacity = 16;
    constexpr uint32_t kRetainedEmissionCapacity = 1024 * 512;

    // Raw world facts only. No chosen per-tile sprites, projected positions or sort keys.
    // kind 1: surface; kind 2: reserved map border. Other values reject the entire emission.
    struct RetainedTile
    {
        int32_t baseZ{};
        int32_t slope{};
        uint32_t grass{};
        uint32_t surfaceMaterial{};
        uint32_t edgeMaterial{};
        uint32_t kind{};
        uint32_t reserved0{};
        uint32_t reserved1{};
    };

    // Compile once per immutable object/asset generation. kind 1 surface, 2 edge.
    // imageBase addresses the normal (non-grid/underground) object image range.
    // Recolour/other effects are not implemented; flags must be zero.
    struct RetainedMaterial
    {
        uint32_t imageBase{};
        uint32_t imageCount{};
        uint32_t kind{};
        uint32_t flags{};
        MaterialLookup selectors{};
    };

    struct RetainedTileChunk
    {
        uint64_t revision{};
        std::array<RetainedTile, kRetainedChunkSize> records{};
    };

    struct RetainedMaterialTable
    {
        uint64_t revision{};
        std::vector<RetainedMaterial> records;
    };

    // Publisher must never retain a mutable alias after publishing. Revisions are
    // nonzero and unique within epoch, including replacement by identical bytes.
    struct RetainedTerrainSnapshot
    {
        uint64_t worldEpoch{};
        std::array<std::shared_ptr<const RetainedTileChunk>, kRetainedChunkCount> chunks;
        std::shared_ptr<const RetainedMaterialTable> materials;
    };

    // Device-owned, unsorted primitive descriptors. This is not a draw command.
    // offsets/bounds follow the frozen painter's camera-relative local arguments;
    // tileIndex identifies the retained world origin. Rear attachedY is screen space.
    // parentIndex identifies the base for rear attachments, otherwise self.
    struct RetainedTerrainPrimitive
    {
        uint32_t tileIndex;
        uint32_t imageIndex;
        uint32_t kind; // 0 base, 1 rear attachment, 2 front parent, 3 border blank
        uint32_t parentIndex;
        int32_t offsetX;
        int32_t offsetY;
        int32_t offsetZ;
        int32_t attachedY;
        int32_t boundsX;
        int32_t boundsY;
        int32_t boundsZ;
        uint32_t rotation;
        uint32_t localOrdinal;
        int32_t edge;
        uint32_t reserved0;
        uint32_t reserved1;
    };

    struct RetainedTerrainStatus
    {
        uint32_t error; // 0 emission valid, 1 unsupported record/material, 2 capacity
        uint32_t primitiveCount; // zero whenever error != 0
        uint32_t firstInvalidTile; // UINT32_MAX if absent
        uint32_t requiredCapacity;
        uint32_t baseCount;
        uint32_t rearCount;
        uint32_t frontCount;
        uint32_t borderCount;
        uint32_t drawingQualified; // always zero: order/sampling/outside-map still pending
        uint32_t reserved0;
        uint32_t reserved1;
        uint32_t reserved2;
    };

    static_assert(std::is_trivially_copyable_v<RetainedTile> && sizeof(RetainedTile) == 32);
    static_assert(offsetof(RetainedTile, kind) == 20);
    static_assert(sizeof(RetainedMaterial) == 592 && offsetof(RetainedMaterial, selectors) == 16);
    static_assert(sizeof(RetainedTerrainPrimitive) == 64);
    static_assert(offsetof(RetainedTerrainPrimitive, offsetX) == 16);
    static_assert(offsetof(RetainedTerrainPrimitive, boundsX) == 32);
    static_assert(offsetof(RetainedTerrainPrimitive, localOrdinal) == 48);
    static_assert(sizeof(RetainedTerrainStatus) == 48);
} // namespace OpenRCT2::Ui::Gpu::Terrain
