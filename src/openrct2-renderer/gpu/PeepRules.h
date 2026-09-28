// Copyright (c) 2014-2026 OpenRCT2 developers. GPL-3.0-or-later.
#pragma once
#include <openrct2/drawing/RetainedPeepState.h>
#include <cstddef>
#include <cstdint>

namespace OpenRCT2::Ui::Gpu::Peeps
{
    using uint = uint32_t;
    using PeepRaw = Drawing::RetainedPeepRecord;
    using PeepLifecycle = Drawing::RetainedPeepLifecycle;
    using PeepMotion = Drawing::RetainedPeepMotion;
    using PeepAppearance = Drawing::RetainedPeepAppearance;
    using PeepAnimation = Drawing::RetainedPeepAnimation;
    using PeepFields = Drawing::RetainedPeepFields;
    using PeepAnimationDescriptor = Drawing::RetainedPeepAnimationDescriptor;
    using PeepAnimationFact = Drawing::RetainedPeepAnimationFact;
#include "../../../data/shaders/vulkan/peep_rules.glsl"
    static_assert(sizeof(PeepRaw) == 96 && offsetof(PeepRaw, generation) == 28);
    static_assert(offsetof(PeepRaw, orientation) == 48 && offsetof(PeepRaw, flags) == 92);
    static_assert(sizeof(PeepAnimationDescriptor) == 32 && sizeof(PeepAnimationFact) == 16);
    static_assert(sizeof(PeepSelection) == 48 && sizeof(PeepProjection) == 48);
}
