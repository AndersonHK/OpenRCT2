// Copyright (c) 2014-2026 OpenRCT2 developers. GPL-3.0-or-later.
#pragma once

#include <cstdint>
#include <vector>

namespace OpenRCT2::Ui::Gpu
{
    // The path slot is one byte in this shared word. Its absent sentinel must not
    // overwrite the separately owned banner identity in the upper half.
    constexpr uint32_t PackWorldObjectMetadata(uint8_t entranceType, uint16_t pathSurfaceSlot, uint16_t bannerId)
    {
        return uint32_t(entranceType) | ((uint32_t(pathSurfaceSlot) & 0xFFu) << 8) | (uint32_t(bannerId) << 16);
    }

    // Authoritative raw fields only. Packing is performed once when an immutable chunk changes.
    struct WorldObjectSourceRecord
    {
        int32_t baseZ{}, clearanceZ{};
        uint32_t ordinal{}, flags{}, objectSlot{}, kind{}, direction{}, sequence{};
        uint32_t colours{}, data0{}, data1{}, reserved{};
        uint32_t trackTypeAndRideType{}, rideIdAndMazeEntry{}, trackData0{}, trackData1{};
    };
    static_assert(sizeof(WorldObjectSourceRecord) == 64);
    using WorldPropSourceRecord = WorldObjectSourceRecord;

    struct WorldPropCatalog
    {
        // Header: four family entry offsets, four family counts, glass palette row.
        // Entries: 16 words. Variable animation and large tile facts follow all entries.
        std::vector<uint32_t> words;
    };
    constexpr uint32_t kWorldPropMaterialWords = 16;
    constexpr uint32_t kWorldPropCatalogHeaderWords = 9;
} // namespace OpenRCT2::Ui::Gpu
