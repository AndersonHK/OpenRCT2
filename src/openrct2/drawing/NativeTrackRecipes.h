// Copyright (c) 2014-2026 OpenRCT2 developers. GPL-3.0-or-later.
#pragma once
#include <cstdint>
#include <span>

namespace OpenRCT2::Drawing
{
    // Immutable source-authored rules, shared by every instance. No live world
    // object or CPU painter is consulted when returning these tables.
    // Header: magic,version,styleCount,typeCount,descriptorWord,rowWord,partWord,totalWords.
    // Descriptor[style*typeCount+type]: firstRow,sequenceCount,stateMask.
    // Rows: firstPart,partCount; compact variant order, then sequence, then direction.
    // State bits: chain=1,inverted=2,brakeClosed=4,cable=8,CSG=16,greenLight=32,platforms=64.
    // sequenceCount=0 means unsupported.
    // Part (12 words): image,offsetXYZ,boundsXYZ,sizeXYZ,colourRole,parentIndex.
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
} // namespace OpenRCT2::Drawing
