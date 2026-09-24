/*****************************************************************************
 * Copyright (c) 2014-2026 OpenRCT2 developers
 *
 * OpenRCT2 is licensed under the GNU General Public License version 3.
 *****************************************************************************/

#pragma once

#include "MapLimits.h"
#include "PathPresentation.h"
#include "TerrainPresentation.h"
#include "WorldBannerPresentation.h"
#include "WorldObjectPresentation.h"
#include "tile_element/TileElement.h"

#include <array>
#include <cstdint>
#include <limits>
#include <memory>
#include <type_traits>
#include <vector>

namespace OpenRCT2
{
    enum class MapPublicationProfile : uint8_t
    {
        legacyTiles,
        rawTerrain,
    };

    /** Pointer-free raw terrain data captured at the map publication boundary; shaders select original art. */
    struct SurfacePresentationRecord
    {
        uint16_t baseZ{};
        uint8_t valid{};
        uint8_t requiresCategoryInterleaving{};
        TerrainPresentationRecord terrain;
    };

    struct MapPresentationTileChange
    {
        uint32_t index{};
        std::vector<TileElement> elements;
        SurfacePresentationRecord surface;
        uint32_t surfaceIndex{ std::numeric_limits<uint32_t>::max() };
        std::vector<PathPresentationRecord> paths;
        std::vector<WorldObjectPresentationRecord> objects;
    };

    struct MapPresentationChangeBatch
    {
        uint64_t epoch{};
        bool reset{};
        uint32_t surfaceWidth{};
        uint32_t surfaceHeight{};
        uint32_t sourceTick{};
        uint8_t clockHour{}, clockMinute{};
        MapPublicationProfile profile{ MapPublicationProfile::legacyTiles };
        std::vector<MapPresentationTileChange> changes;
        std::shared_ptr<const TerrainPresentationMaterials> terrainMaterials;
        std::shared_ptr<const PathPresentationMaterials> pathMaterials;
        std::shared_ptr<const WorldObjectPresentationMaterials> objectMaterials;
        std::shared_ptr<const WorldRidePresentationMaterials> rideMaterials;
        std::shared_ptr<const WorldRidePoseSnapshot> ridePoses;
        std::shared_ptr<const WorldBannerPresentation> bannerTexts;
    };

    /** Owned tile storage and O(1) tile lookup for one presentation frame. */
    class MapPresentationSnapshot
    {
    public:
        static constexpr size_t kChunkWidth = 256;
        static constexpr size_t kTileCount = kMaximumMapSizeTechnical * kMaximumMapSizeTechnical;
        static constexpr size_t kChunkCount = (kTileCount + kChunkWidth - 1) / kChunkWidth;

        struct SurfaceChunk
        {
            uint64_t revision{};
            std::array<SurfacePresentationRecord, kChunkWidth> records{};
        };

        struct PathChunk
        {
            uint64_t revision{};
            std::array<PathPresentationTileRange, kChunkWidth> tiles{};
            std::vector<PathPresentationRecord> records;
        };
        struct ObjectChunk
        {
            uint64_t revision{};
            std::array<WorldObjectPresentationTileRange, kChunkWidth> tiles{};
            std::vector<WorldObjectPresentationRecord> records;
        };
        using ObjectChunks = std::vector<std::shared_ptr<const ObjectChunk>>;
        using PathChunks = std::vector<std::shared_ptr<const PathChunk>>;
        using SurfaceChunks = std::vector<std::shared_ptr<const SurfaceChunk>>;

    private:
        using Chunk = std::array<std::vector<TileElement>, kChunkWidth>;

        // Native publications never allocate the legacy tile directory. Snapshot header copies
        // share both immutable directories; Apply clones only directories with actual changes.
        using Chunks = std::vector<std::shared_ptr<const Chunk>>;
        std::shared_ptr<const Chunks> _chunks;
        std::shared_ptr<const SurfaceChunks> _surfaceChunks;
        inline static const SurfaceChunks kEmptySurfaceChunks{};
        std::shared_ptr<const PathChunks> _pathChunks;
        inline static const PathChunks kEmptyPathChunks{};
        uint64_t _nextPathRevision{};
        std::shared_ptr<const ObjectChunks> _objectChunks;
        inline static const ObjectChunks kEmptyObjectChunks{};
        uint64_t _nextObjectRevision{};
        using ObjectOccurrenceCounts = std::array<
            std::array<uint32_t, WorldObjectPresentationUsage::kSlots>, WorldObjectPresentationUsage::kKinds>;
        std::shared_ptr<const ObjectOccurrenceCounts> _objectOccurrences;
        std::shared_ptr<const WorldObjectPresentationUsage> _objectUsage;
        std::shared_ptr<const WorldObjectPresentationMaterials> _objectMaterials;
        std::shared_ptr<const WorldRidePresentationMaterials> _rideMaterials;
        std::shared_ptr<const WorldRidePoseSnapshot> _ridePoses;
        std::shared_ptr<const WorldBannerPresentation> _bannerTexts;
        uint8_t _clockHour{}, _clockMinute{};
        std::shared_ptr<const PathPresentationMaterials> _pathMaterials;
        uint64_t _epoch{};
        uint32_t _sourceTick{};
        MapPublicationProfile _profile{ MapPublicationProfile::legacyTiles };
        uint64_t _nextSurfaceRevision{};
        uint32_t _surfaceWidth{};
        uint32_t _surfaceHeight{};
        uint16_t _surfaceBaselineZ{};
        bool _surfaceBaselineSet{};
        uint32_t _surfaceBlockingRecordCount{};
        uint32_t _terrainBlockingRecordCount{};
        std::shared_ptr<const TerrainPresentationMaterials> _terrainMaterials;

    public:
        [[nodiscard]] const std::shared_ptr<const TerrainPresentationMaterials>& GetTerrainMaterials() const noexcept
        {
            return _terrainMaterials;
        }
        [[nodiscard]] const PathChunks& GetPathChunks() const noexcept
        {
            return _pathChunks == nullptr ? kEmptyPathChunks : *_pathChunks;
        }
        [[nodiscard]] const std::shared_ptr<const PathPresentationMaterials>& GetPathMaterials() const noexcept
        {
            return _pathMaterials;
        }
        [[nodiscard]] const ObjectChunks& GetObjectChunks() const noexcept
        {
            return _objectChunks == nullptr ? kEmptyObjectChunks : *_objectChunks;
        }
        [[nodiscard]] const std::shared_ptr<const WorldObjectPresentationUsage>& GetObjectUsage() const noexcept
        {
            return _objectUsage;
        }
        [[nodiscard]] const std::shared_ptr<const WorldObjectPresentationMaterials>& GetObjectMaterials() const noexcept
        {
            return _objectMaterials;
        }
        [[nodiscard]] const std::shared_ptr<const WorldRidePresentationMaterials>& GetRideMaterials() const noexcept
        {
            return _rideMaterials;
        }
        [[nodiscard]] const std::shared_ptr<const WorldRidePoseSnapshot>& GetRidePoses() const noexcept
        {
            return _ridePoses;
        }
        [[nodiscard]] const std::shared_ptr<const WorldBannerPresentation>& GetBannerTexts() const noexcept
        {
            return _bannerTexts;
        }
        [[nodiscard]] uint8_t GetClockHour() const noexcept
        {
            return _clockHour;
        }
        [[nodiscard]] uint8_t GetClockMinute() const noexcept
        {
            return _clockMinute;
        }
        [[nodiscard]] bool HasBoundedTerrainFacts() const noexcept
        {
            return _surfaceWidth == 32 && _surfaceHeight == 32 && _terrainBlockingRecordCount == 0
                && _terrainMaterials != nullptr;
        }
        void Apply(const MapPresentationChangeBatch& batch);
        [[nodiscard]] uint32_t GetSourceTick() const noexcept
        {
            return _sourceTick;
        }
        [[nodiscard]] bool IsRawTerrainOnly() const noexcept
        {
            return _profile == MapPublicationProfile::rawTerrain;
        }
        [[nodiscard]] bool HasLegacyTileStorage() const noexcept
        {
            return _chunks != nullptr;
        }
        [[nodiscard]] TileElement* GetFirstElementAt(const TileCoordsXY& tilePos) const;
        [[nodiscard]] uint64_t GetEpoch() const noexcept
        {
            return _epoch;
        }
        [[nodiscard]] const SurfaceChunks& GetSurfaceChunks() const noexcept
        {
            return _surfaceChunks == nullptr ? kEmptySurfaceChunks : *_surfaceChunks;
        }
        [[nodiscard]] uint32_t GetSurfaceWidth() const noexcept
        {
            return _surfaceWidth;
        }
        [[nodiscard]] uint32_t GetSurfaceHeight() const noexcept
        {
            return _surfaceHeight;
        }
        [[nodiscard]] uint32_t GetSurfaceRecordCount() const noexcept
        {
            return _surfaceWidth * _surfaceHeight;
        }
        [[nodiscard]] bool CanDrawSurfaceBaseIndependently() const noexcept
        {
            return GetSurfaceRecordCount() != 0 && _surfaceBaselineSet && _surfaceBlockingRecordCount == 0;
        }
    };

    static_assert(std::is_trivially_copyable_v<SurfacePresentationRecord>);

    // The sole publication owner requests a complete bootstrap only when it has no front snapshot.
    // If an earlier owner already consumed the reset, this gives the new snapshot a fresh epoch so
    // GPU chunk revisions cannot alias resident records from the discarded publication.
    [[nodiscard]] MapPresentationChangeBatch ConsumeMapPresentationChanges(
        bool requireCompleteSnapshot = false, MapPublicationProfile profile = MapPublicationProfile::legacyTiles);
    [[nodiscard]] uint64_t GetMapPresentationEpoch() noexcept;
    // Owner-thread, complete native raw snapshot for an auxiliary job. Does not consume dirty input,
    // alter the live map epoch, or publish into the main-window generation.
    [[nodiscard]] std::shared_ptr<const MapPresentationSnapshot> CaptureAuxiliaryMapPresentationSnapshot();

    /** Installs a snapshot only for map reads made by the current paint worker. */
    class ScopedMapPresentationSnapshot
    {
    private:
        const MapPresentationSnapshot* _previous{};

    public:
        explicit ScopedMapPresentationSnapshot(const MapPresentationSnapshot* snapshot) noexcept;
        ~ScopedMapPresentationSnapshot();

        ScopedMapPresentationSnapshot(const ScopedMapPresentationSnapshot&) = delete;
        ScopedMapPresentationSnapshot& operator=(const ScopedMapPresentationSnapshot&) = delete;
    };
} // namespace OpenRCT2
