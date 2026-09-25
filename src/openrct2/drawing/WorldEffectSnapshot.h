// Copyright (c) 2014-2026 OpenRCT2 developers. GPL-3.0-or-later.
#pragma once
#include <cstdint>
#include <memory>
#include <vector>
namespace OpenRCT2::Drawing
{
    struct WorldEffectRecord
    {
        int32_t x{}, y{}, z{};
        uint32_t type{}, orientation{}, frame{}, state{}, colours{};
        bool operator==(const WorldEffectRecord&) const = default;
    };
    static_assert(sizeof(WorldEffectRecord) == 32);
    struct WorldEffectSnapshot
    {
        uint64_t epoch{};
        uint32_t sourceTick{};
        std::vector<WorldEffectRecord> records;
    };
    std::shared_ptr<const WorldEffectSnapshot> CaptureWorldEffects(uint32_t sourceTick);
} // namespace OpenRCT2::Drawing
