// Copyright (c) 2014-2026 OpenRCT2 developers. GPL-3.0-or-later.
#pragma once
#include "TextGlyphRun.h"

#include <memory>

namespace OpenRCT2::Drawing
{
    struct MoneyPresentationRecord
    {
        int32_t x{}, y{}, z{};
        uint32_t entityId{}, generation{}, wiggle{};
        int32_t offsetX{};
        uint32_t run{}, flags{}; // bit0 guest purchase (visibility captured in snapshot).
        bool operator==(const MoneyPresentationRecord&) const = default;
    };
    struct MoneyGlyphCatalog
    {
        std::vector<std::shared_ptr<const TextGlyphRun>> runs;
    };
    struct MoneyPresentationSnapshot
    {
        uint64_t epoch{}, revision{};
        uint32_t sourceTick{};
        std::shared_ptr<const std::vector<MoneyPresentationRecord>> records;
        std::shared_ptr<const MoneyGlyphCatalog> catalog;
    };
    std::shared_ptr<const MoneyPresentationSnapshot> CaptureMoneyPresentationSnapshot(uint32_t sourceTick);
} // namespace OpenRCT2::Drawing
