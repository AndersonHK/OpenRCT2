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
    // State bits: chain=1,inverted=2,brakeClosed=4. sequenceCount=0 means unsupported.
    // Part (12 words): image,offsetXYZ,boundsXYZ,sizeXYZ,colourRole,parentIndex.
    // Colour role0 is track main/additional; role1 is support/additional.
    // Geometry is camera-relative, with Z relative to the raw track base height.
    // Parent index -1 means a parent; other indices are local to that recipe.
    std::span<const uint32_t> GetNativeTrackRecipeWords();

    // Enumerated once from shared definitions, never per tile or per frame.
    // Admission must resolve their original G1 art/zoom chain and hold atlas leases.
    std::span<const uint32_t> GetNativeTrackRecipeImages();
} // namespace OpenRCT2::Drawing
