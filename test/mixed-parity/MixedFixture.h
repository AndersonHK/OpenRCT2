// STAGED ONLY. Unbuilt and unqualified. No production admission.
#pragma once
#include <array>
#include <cstddef>
#include <cstdint>

namespace OpenRCT2::Ui::Gpu::MixedFixture
{
    constexpr uint32_t kOwnerCapacity = 256;
    constexpr uint32_t kPartsPerOwner = 4;
    constexpr uint32_t kCommandCapacity = kOwnerCapacity * kPartsPerOwner;
    // Persistent world inputs; definitionBase points at four contiguous rotation
    // rows compiled at immutable fixture/object load, never a per-frame image.
    struct StaticInstance
    {
        int32_t x, y, z;
        uint32_t id;
        uint32_t definitionBase, objectKey, objectGeneration, present;
    };
    // Original painter-local static component facts. parentPart == UINT32_MAX
    // denotes an independent parent; otherwise it is an earlier parent in this
    // same definition and this part shares its anchor/depth plus attachedY.
    struct StaticPart
    {
        uint32_t image, palettes, effects, parentPart;
        int32_t offsetX, offsetY, offsetZ, attachedY;
        int32_t boundsX, boundsY, boundsZ, sizeX;
        int32_t sizeY, sizeZ;
        uint32_t reserved0, reserved1;
    };
    struct StaticDefinition
    {
        uint32_t objectKey, objectGeneration, rotation, partCount;
        std::array<StaticPart, kPartsPerOwner> parts;
    };
    // All instances share these camera constants. Zoom0 only in first slice.
    struct Camera
    {
        int32_t x, y, width, height;
        int32_t clipX, clipY;
        uint32_t rotation, pass;
        uint32_t staticCount, peepCount, definitionCount, descriptorCount;
        uint32_t factCount, spriteCount, assetCount, outputCapacity;
    };
    static_assert(sizeof(StaticInstance) == 32);
    static_assert(sizeof(StaticPart) == 64 && offsetof(StaticPart, boundsX) == 32);
    static_assert(sizeof(StaticDefinition) == 272);
    static_assert(sizeof(Camera) == 64);
    // Reuse Terrain::DrawSpriteMetadata (96 bytes), Peeps::PeepRaw (96),
    // PeepAnimationDescriptor (32), and PeepAnimationFact (16).
    // GPU scratch SpriteCommand slots: 1024 * 60 bytes.
    // Status words: error, diagnosticCount, reserved, reserved, then 1024 validity
    // bits represented as uints, followed by VkDrawIndirectCommand at byte4112.
    constexpr uint32_t kIndirectWord = 4 + kCommandCapacity;
    constexpr uint32_t kStatusWords = kIndirectWord + 4;
    constexpr uint32_t kIndirectByteOffset = kIndirectWord * sizeof(uint32_t);
}
