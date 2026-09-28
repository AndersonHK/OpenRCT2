// Copyright (c) 2014-2026 OpenRCT2 developers. GPL-3.0-or-later.
// Shared GPU selector rules. Also compiled as C++ by independent frozen-trace tests.
// These rules do not admit a world scene: culling, residency, interpolation,
// lighting and cross-family ordering must be qualified by the consuming pass.
#ifndef OPENRCT2_PEEP_RULES
#define OPENRCT2_PEEP_RULES
#ifdef __cplusplus
#define PEEP_FN constexpr
#else
#define PEEP_FN
struct PeepRaw
{
    int x; int y; int z; uint id;
    int previousX; int previousY; int previousZ; uint generation;
    uint objectIndex; uint objectGeneration; uint sourceTick; uint previousTick;
    uint orientation; uint action; uint animationGroup; uint animationType;
    uint nextAnimationType; uint frameOffset; uint colours; uint accessoryColours;
    uint width; uint heightMin; uint heightMax; uint flags;
};
struct PeepLifecycle { uint generation; uint flags; };
struct PeepMotion
{
    int x; int y; int z; uint orientation;
    int previousX; int previousY; int previousZ; uint sourceTick; uint previousTick; uint flags;
};
struct PeepAppearance { uint objectIndex; uint objectGeneration; uint colours; uint accessoryColours; };
struct PeepAnimation
{
    uint action; uint animationGroup; uint animationType; uint nextAnimationType; uint frameOffset;
    uint width; uint heightMin; uint heightMax; uint flags;
};
struct PeepFields
{
    uint id; PeepLifecycle lifecycle; PeepMotion motion; PeepAppearance appearance; PeepAnimation animation;
};
struct PeepAnimationDescriptor
{
    uint objectIndex; uint objectGeneration; uint groupCount; uint factOffset;
    uint imageBase; uint imageCount; uint reserved0; uint reserved1;
};
struct PeepAnimationFact { uint baseImage; uint valid; uint reserved0; uint reserved1; };
#endif

// The diagnostic consumes this schema directly. A future resident world pass gathers each group by stable slot ID
// after committing one coherent field revision; it must not advance individual buffers independently.
PEEP_FN PeepRaw peepJoinFields(PeepFields f)
{
    PeepRaw r;
    r.id = f.id; r.generation = f.lifecycle.generation;
    r.flags = f.lifecycle.flags | f.motion.flags | f.animation.flags;
    r.x = f.motion.x; r.y = f.motion.y; r.z = f.motion.z; r.orientation = f.motion.orientation;
    r.previousX = f.motion.previousX; r.previousY = f.motion.previousY; r.previousZ = f.motion.previousZ;
    r.sourceTick = f.motion.sourceTick; r.previousTick = f.motion.previousTick;
    r.objectIndex = f.appearance.objectIndex; r.objectGeneration = f.appearance.objectGeneration;
    r.colours = f.appearance.colours; r.accessoryColours = f.appearance.accessoryColours;
    r.action = f.animation.action; r.animationGroup = f.animation.animationGroup;
    r.animationType = f.animation.animationType; r.nextAnimationType = f.animation.nextAnimationType;
    r.frameOffset = f.animation.frameOffset; r.width = f.animation.width;
    r.heightMin = f.animation.heightMin; r.heightMax = f.animation.heightMax;
    return r;
}

// Invalid records return no lookup/image and require the owning GPU pass to
// reject publication. A zero-emission valid culled record is not an error.
struct PeepSelection
{
    uint error; uint direction; uint animationType; uint frameOffset;
    uint parentImage; uint childImage; uint parentRemapCount; uint parentPrimary;
    uint parentSecondary; uint childPrimary; uint childPresent; uint reserved;
};
struct PeepProjection
{
    int screenX; int screenY; int drawX; int drawY;
    int x; int y; int z; int xEnd; int yEnd; int zEnd;
    uint error; uint reserved;
};

PEEP_FN uint peepAnimationType(PeepRaw raw)
{
    return raw.action == 254u ? raw.nextAnimationType : raw.animationType;
}

PEEP_FN uint peepRecordError(PeepRaw raw)
{
    if ((raw.flags & 1u) == 0u || (raw.flags & ~0xff07u) != 0u || raw.generation == 0u
        || raw.orientation > 31u || raw.action > 255u || raw.frameOffset > 255u
        || raw.animationGroup > 255u || peepAnimationType(raw) >= 37u
        || raw.width > 255u || raw.heightMin > 255u || raw.heightMax > 255u)
        return 1u;
    return 0u;
}

// Call in the shader before reading the fact SSBO. No CPU image selector.
PEEP_FN uint peepFactAddress(PeepRaw raw, PeepAnimationDescriptor table, uint availableFacts)
{
    if (peepRecordError(raw) != 0u || table.objectIndex != raw.objectIndex
        || table.objectGeneration == 0u || table.objectGeneration != raw.objectGeneration
        || table.reserved0 != 0u || table.reserved1 != 0u || table.groupCount == 0u || table.groupCount > 256u
        || raw.animationGroup >= table.groupCount || table.factOffset > availableFacts
        || table.imageCount == 0u || table.imageBase == 0xffffffffu
        || table.imageCount > 0xffffffffu - table.imageBase)
        return 0xffffffffu;
    // Validate the entire object table, not just this peep's selected row. The
    // subtraction form cannot wrap even for hostile offsets/capacities.
    if (table.groupCount * 37u > availableFacts - table.factOffset)
        return 0xffffffffu;
    uint relative = raw.animationGroup * 37u + peepAnimationType(raw);
    if (relative >= availableFacts - table.factOffset)
        return 0xffffffffu;
    return table.factOffset + relative;
}

// `fact` is the validated SSBO row at peepFactAddress. Allocation/residency
// validation of both resulting images is a separate mandatory catalog gate.
PEEP_FN PeepSelection peepSelect(PeepRaw raw, PeepAnimationDescriptor table, PeepAnimationFact fact,
    uint rotation, uint availableFacts)
{
    PeepSelection outValue;
    outValue.error = 0u; outValue.direction = 0u; outValue.animationType = 0u; outValue.frameOffset = 0u;
    outValue.parentImage = 0xffffffffu; outValue.childImage = 0xffffffffu;
    outValue.parentRemapCount = 0u; outValue.parentPrimary = 0u; outValue.parentSecondary = 0u;
    outValue.childPrimary = 0u; outValue.childPresent = 0u; outValue.reserved = 0u;
    if (rotation > 3u || peepFactAddress(raw, table, availableFacts) == 0xffffffffu
        || fact.valid != 1u || fact.reserved0 != 0u || fact.reserved1 != 0u)
    { outValue.error = 1u; return outValue; }
    outValue.direction = ((rotation * 8u + raw.orientation) & 31u) >> 3u;
    outValue.animationType = peepAnimationType(raw);
    outValue.frameOffset = raw.action == 254u ? 0u : raw.frameOffset;
    uint offset = outValue.animationType == 11u ? outValue.frameOffset
        : outValue.direction + outValue.frameOffset * 4u;
    if (table.imageCount == 0u || fact.baseImage < table.imageBase
        || fact.baseImage - table.imageBase >= table.imageCount
        || offset >= table.imageCount - (fact.baseImage - table.imageBase)
        || fact.baseImage >= 0xffffffffu - offset)
    { outValue.error = 2u; return outValue; }
    outValue.parentImage = fact.baseImage + offset;
    outValue.parentRemapCount = (raw.flags & 2u) != 0u ? 1u : 2u;
    outValue.parentPrimary = raw.colours & 255u;
    outValue.parentSecondary = (raw.flags & 2u) != 0u ? 0u : (raw.colours >> 8u) & 255u;
    if ((raw.flags & 2u) != 0u || raw.action == 11u || raw.action == 26u || raw.action == 8u)
        return outValue;
    uint base = 0u;
    if (raw.animationGroup == 15u)
    { base = 10749u; outValue.childPrimary = raw.accessoryColours & 255u; }
    else if (raw.animationGroup == 5u)
    { base = 10813u; outValue.childPrimary = (raw.accessoryColours >> 8u) & 255u; }
    else if (raw.animationGroup == 7u)
    { base = 11229u; outValue.childPrimary = (raw.accessoryColours >> 16u) & 255u; }
    if (base == 0u)
        return outValue;
    uint itemFrame = outValue.frameOffset % 6u;
    if (outValue.animationType == 2u) itemFrame = 6u;
    else if (outValue.animationType == 7u) itemFrame = 7u;
    outValue.childPresent = 1u;
    outValue.childImage = base + outValue.direction + itemFrame * 4u;
    return outValue;
}

// Anchors are world-plane pixels. Entity zoom snapping precedes the existing
// atlas raster/zoom rules. Child and parent get this same anchor independently.
// No interpolation is admitted by this helper: x/y/z are the supplied state.
PEEP_FN PeepProjection peepProject(PeepRaw raw, uint rotation, int zoom)
{
    PeepProjection result;
    result.screenX = 0; result.screenY = 0; result.drawX = 0; result.drawY = 0;
    result.x = 0; result.y = 0; result.z = 0; result.xEnd = 0; result.yEnd = 0; result.zEnd = 0;
    result.error = 0u; result.reserved = 0u;
    if (peepRecordError(raw) != 0u || rotation > 3u || zoom < -2 || zoom > 2
        || raw.x < -1048576 || raw.x > 1048576 || raw.y < -1048576 || raw.y > 1048576
        || raw.z < -1048576 || raw.z > 1048576)
    { result.error = 1u; return result; }
    int x = raw.x; int y = raw.y;
    if (rotation == 1u) { x = raw.y; y = -raw.x; }
    else if (rotation == 2u) { x = -raw.x; y = -raw.y; }
    else if (rotation == 3u) { x = -raw.y; y = raw.x; }
    result.screenX = y - x;
    result.screenY = ((x + y) >> 1) - raw.z;
    int mask = zoom == 2 ? -4 : (zoom == 1 ? -2 : -1);
    result.drawX = result.screenX & mask;
    result.drawY = result.screenY & mask;
    result.x = raw.x; result.y = raw.y; result.z = raw.z + 5;
    // Exact RotateBoundBoxSize({1,1,11}, rotation), including reversed ends.
    result.xEnd = raw.x - (rotation == 1u || rotation == 2u ? 1 : 0);
    result.yEnd = raw.y - (rotation == 2u || rotation == 3u ? 1 : 0);
    result.zEnd = raw.z + 16;
    return result;
}

// Body/accessory G1 culls are independent. Frozen AddChild promotes a surviving
// child to parent if the body did not allocate. Return0=none,1=parent,2=child.
PEEP_FN uint peepAccessoryRelation(uint childPresent, uint bodyAllocated, uint accessoryAllocated)
{
    if (childPresent == 0u || accessoryAllocated == 0u) return 0u;
    return bodyAllocated != 0u ? 2u : 1u;
}
#undef PEEP_FN
#endif
