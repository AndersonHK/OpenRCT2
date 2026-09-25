// Copyright (c) 2014-2026 OpenRCT2 developers. GPL-3.0-or-later.
#pragma once
#include <cstdint>
#include <span>

namespace OpenRCT2::Drawing
{
    // Image-word tag: bit31 animated, base bits0..18, log2(tick period)
    // bits19..21, log2(frame count) bits22..24. Other high bits must be zero.
    // Only unsigned power-of-two clocks with 2..16 contiguous frames are admitted.
    constexpr bool IsNativeTrackAnimatedImage(uint32_t image)
    {
        const auto shift = (image >> 19) & 7u, frames = (image >> 22) & 7u;
        return (image & 0xfe000000u) == 0x80000000u && shift <= 3 && frames >= 1 && frames <= 4
            && (image & 0x7ffffu) + (1u << frames) <= 0x7ffffu;
    }
    constexpr uint32_t GetNativeTrackImageFrameCount(uint32_t image)
    {
        return IsNativeTrackAnimatedImage(image) ? 1u << ((image >> 22) & 7u) : 1u;
    }
    constexpr uint32_t GetNativeTrackImageAtTick(uint32_t image, uint32_t tick)
    {
        return IsNativeTrackAnimatedImage(image)
            ? (image & 0x7ffffu) + ((tick >> ((image >> 19) & 7u)) & (GetNativeTrackImageFrameCount(image) - 1u))
            : image;
    }

    // Immutable source-authored rules, shared by every instance. No live world
    // object or CPU painter is consulted when returning these tables.
    // Header: magic,version,styleCount,typeCount,descriptorWord,rowWord,partWord,totalWords.
    // Descriptor[style*typeCount+type]: firstRow,sequenceCount,stateMask.
    // Rows: firstPart,partCount; compact variant order, then sequence, then direction.
    // State bits: chain=1,inverted=2,brakeClosed=4,cable=8,CSG=16,greenLight=32,platforms=64.
    // sequenceCount=0 means unsupported.
    // Part (12 words): image,offsetXYZ,boundsXYZ,sizeXYZ,colourRole,parentIndex.
    // colourRole low3 bits select palette; bit3 marks source-named frontTrack/frontHandrail contact.
    // Colour roles: 0=main/additional, 1=support/additional, 2=black, 3=main/support.
    // Image0xfffffffe marks procedural station geometry; size.y selects the
    // regular/inverted/narrow/pier recipe. It is never an original sprite ID.
    // Image0xfffffffd marks a tunnel request, not a drawable component:
    // offset.x=left/right/vertical (0/1/2), offset.y=TunnelType (or 256+
    // doorB/inward/flatToDown25 selector bits0/1/2), offset.z=relative
    // world height. Remaining fields zero, parent=-1. Source call order is retained.
    // Image0xfffffffc marks three photo parents: offset.x=direction,
    // offset.y=small-art flag, offset.z=relative height. GPU camera/flash
    // selection uses the captured photo timeout; colour role is black (2).
    // Geometry is camera-relative, with Z relative to the raw track base height.
    // Parent index -1 means a parent; other indices are local to that recipe.
    std::span<const uint32_t> GetNativeTrackRecipeWords();

    // Enumerated once from shared definitions, never per tile or per frame.
    // Admission must resolve their original G1 art/zoom chain and hold atlas leases.
    std::span<const uint32_t> GetNativeTrackRecipeImages();

    // Separate support program; rail definitions and their parent indices are unchanged.
    // Header/descriptor/row layout matches recipes, magic=0x54535054, version=1.
    // Up to64 ordered operations per row, 12 words each:
    // opcode,type,placement,rotation,height,extra,segmentMask,slope,predicate,
    // colourRole,railOrdinal,flags. Opcode: 1=metalA,2=metalB,3=segments,4=general,5=woodenA,6=woodenB.
    // Type0..7 or255=ride descriptor metal type; rotation0..3 or4=unrotated.
    // Heights are signed track-relative, except segment height65535 sentinel.
    // Segment masks use PaintSegment bits (not MetalSupportPlace numbering).
    // Predicate: 0=always,1=tile X/Y parity equal,2=parity unequal.
    // Flag1: placement is already rotated, rotation still selects graphic.
    // Wooden: type0=truss,1=mine,255=ride descriptor; placement is subtype0..5,
    // rotation0..3 always selects transition direction, extra is transition0..20
    // or255=none. Flag2 rotates subtype. Flag4 binds an original prepend owner:
    // segmentMask stores its qualified baseline recipe ordinal (tunnel slots included).
    // The named owner must exist; it cannot fall back to an independent parent.
    // Unavailable programs have sequenceCount0; coverage gaps never remove rails.
    std::span<const uint32_t> GetNativeTrackSupportWords();
} // namespace OpenRCT2::Drawing
