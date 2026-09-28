/*****************************************************************************
 * Copyright (c) 2014-2026 OpenRCT2 developers
 *
 * For a complete list of all authors, please refer to contributors.md
 * Interested in contributing? Visit https://github.com/OpenRCT2/OpenRCT2
 *
 * OpenRCT2 is licensed under the GNU General Public License version 3.
 *****************************************************************************/

#pragma once

#include "../core/EnumUtils.hpp"
#include "../world/MapLimits.h"
#include "EntityBase.h"
#include "EntityVisualLifecycle.h"

#include <array>
#include <bit>
#include <bitset>
#include <cstddef>
#include <cstdint>
#include <iterator>
#include <memory>
#include <span>
#include <string>
#include <type_traits>
#include <vector>

struct CoordsXY;

namespace OpenRCT2
{
    constexpr uint16_t kMaxEntities = 65535;
    constexpr uint16_t kMaxMiscEntities = 3200;
    static_assert(EntityId::GetNull().ToUnderlying() == kMaxEntities);

    constexpr const uint32_t kSpatialIndexSize = (kMaximumMapSizeTechnical * kMaximumMapSizeTechnical) + 1;
    constexpr uint32_t kSpatialIndexNullBucket = kSpatialIndexSize - 1;

    constexpr uint32_t kInvalidSpatialIndex = 0xFFFFFFFFu;
    constexpr uint32_t kSpatialIndexDirtyMask = 1u << 31;

    class EntityRegistry;
    class EntityPresentationSnapshot;
    class EntityStorage;
    namespace Drawing
    {
        struct RetainedEntityPublicationInput;
    }

    // Allocation-free ascending membership with the former list iterator's mutation semantics.
    class EntityIdList
    {
    private:
        friend class EntityRegistry;

        static constexpr size_t kBitsPerWord = 64;
        static constexpr size_t kWordCount = (kMaxEntities + kBitsPerWord - 1) / kBitsPerWord;
        static constexpr size_t kSummaryWordCount = (kWordCount + kBitsPerWord - 1) / kBitsPerWord;
        static constexpr uint32_t kEndIndex = kMaxEntities;

        std::array<uint64_t, kWordCount> _membership{};
        std::array<uint64_t, kSummaryWordCount> _nonEmptyWords{};
        uint32_t _size{};

        [[nodiscard]] size_t FindNextNonEmptyWord(size_t start) const noexcept
        {
            if (start >= kWordCount)
                return kWordCount;

            const auto firstSummaryWord = start / kBitsPerWord;
            for (auto summaryIndex = firstSummaryWord; summaryIndex < kSummaryWordCount; summaryIndex++)
            {
                auto summary = _nonEmptyWords[summaryIndex];
                if (summaryIndex == firstSummaryWord)
                    summary &= ~uint64_t{ 0 } << (start % kBitsPerWord);
                if (summary != 0)
                {
                    const auto wordIndex = (summaryIndex * kBitsPerWord) + static_cast<size_t>(std::countr_zero(summary));
                    return wordIndex;
                }
            }
            return kWordCount;
        }

        [[nodiscard]] uint32_t FindNext(uint32_t start) const noexcept
        {
            if (start >= kMaxEntities)
                return kEndIndex;

            auto wordIndex = static_cast<size_t>(start / kBitsPerWord);
            auto word = _membership[wordIndex] & (~uint64_t{ 0 } << (start % kBitsPerWord));
            if (word == 0)
            {
                wordIndex = FindNextNonEmptyWord(wordIndex + 1);
                if (wordIndex == kWordCount)
                    return kEndIndex;
                word = _membership[wordIndex];
            }
            const auto index = static_cast<uint32_t>((wordIndex * kBitsPerWord) + static_cast<size_t>(std::countr_zero(word)));
            return index < kMaxEntities ? index : kEndIndex;
        }

        [[nodiscard]] bool Contains(uint32_t index) const noexcept
        {
            return (_membership[index / kBitsPerWord] & (uint64_t{ 1 } << (index % kBitsPerWord))) != 0;
        }

        bool SetMembership(EntityId id, bool present) noexcept
        {
            const auto index = id.ToUnderlying();
            if (index >= kMaxEntities)
                return false;
            const auto mask = uint64_t{ 1 } << (index % kBitsPerWord);
            const auto wordIndex = index / kBitsPerWord;
            auto& word = _membership[wordIndex];
            if (((word & mask) != 0) == present)
                return false;
            const bool wordWasEmpty = word == 0;
            word ^= mask;
            if (wordWasEmpty != (word == 0))
            {
                const auto summaryMask = uint64_t{ 1 } << (wordIndex % kBitsPerWord);
                _nonEmptyWords[wordIndex / kBitsPerWord] ^= summaryMask;
            }
            _size = present ? _size + 1 : _size - 1;
            return true;
        }

        bool insert(EntityId id) noexcept
        {
            return SetMembership(id, true);
        }
        bool erase(EntityId id) noexcept
        {
            return SetMembership(id, false);
        }

        void clear() noexcept
        {
            _membership.fill(0);
            _nonEmptyWords.fill(0);
            _size = 0;
        }

        void fill() noexcept
        {
            _membership.fill(~uint64_t{ 0 });
            constexpr auto validBitsInLastWord = kMaxEntities % kBitsPerWord;
            if constexpr (validBitsInLastWord != 0)
                _membership.back() &= (uint64_t{ 1 } << validBitsInLastWord) - 1;
            _nonEmptyWords.fill(~uint64_t{ 0 });
            constexpr auto validWordsInLastSummary = kWordCount % kBitsPerWord;
            if constexpr (validWordsInLastSummary != 0)
                _nonEmptyWords.back() &= (uint64_t{ 1 } << validWordsInLastSummary) - 1;
            _size = kMaxEntities;
        }

    public:
        class const_iterator
        {
        private:
            friend class EntityIdList;

            const EntityIdList* _list{};
            uint32_t _index{ kEndIndex };
            uint32_t _nextIndex{ kEndIndex };

            const_iterator(const EntityIdList& list, uint32_t index)
                : _list(&list)
                , _index(index)
                , _nextIndex(index < kEndIndex ? list.FindNext(index + 1) : kEndIndex)
            {
            }

        public:
            using difference_type = std::ptrdiff_t;
            using value_type = EntityId;
            using pointer = void;
            using reference = EntityId;
            using iterator_category = std::forward_iterator_tag;

            const_iterator() = default;

            EntityId operator*() const noexcept
            {
                return EntityId::FromUnderlying(static_cast<EntityId::UnderlyingType>(_index));
            }

            const_iterator& operator++() noexcept
            {
                // Preserve the old list iterator's mutation semantics: current removal is safe and removed lookahead is
                // skipped.
                _index = _nextIndex;
                if (_index < kEndIndex && !_list->Contains(_index))
                    _index = _list->FindNext(_index + 1);
                _nextIndex = _index < kEndIndex ? _list->FindNext(_index + 1) : kEndIndex;
                return *this;
            }

            const_iterator operator++(int) noexcept
            {
                auto previous = *this;
                ++(*this);
                return previous;
            }

            bool operator==(const const_iterator& other) const noexcept
            {
                return _list == other._list && _index == other._index;
            }
        };

        [[nodiscard]] const_iterator begin() const noexcept
        {
            return const_iterator(*this, FindNext(0));
        }

        [[nodiscard]] const_iterator end() const noexcept
        {
            return const_iterator(*this, kEndIndex);
        }

        [[nodiscard]] size_t size() const noexcept
        {
            return _size;
        }
        [[nodiscard]] bool empty() const noexcept
        {
            return _size == 0;
        }
    };

    static_assert(sizeof(EntityIdList) <= 9 * 1024, "Entity id membership should remain cache compact");

    union Entity_t
    {
        uint8_t pad00[0x200];
        EntityBase base;
        Entity_t()
            : pad00()
        {
        }
    };

#pragma pack(push, 1)
    struct EntitiesChecksum
    {
        std::array<std::byte, 20> raw;

        std::string toString() const;
    };
#pragma pack(pop)

    template<typename T>
    class EntityList;
    template<typename T>
    class EntityListIterator;

    class EntityRegistry
    {
    private:
        friend class EntityPresentationSnapshot;
        friend struct EntityBase;
        template<typename T>
        friend class EntityListIterator;

        std::unique_ptr<EntityStorage> _storage;
        std::array<EntityBase*, kMaxEntities> entities{};
        std::array<uint32_t, kMaxEntities> _entityPoolSlots{};
        std::array<EntityIdList, EnumValue(EntityType::count)> gEntityLists;
        std::array<std::vector<EntityBase*>, EnumValue(EntityType::count)> _entityExecutionLists;
        std::vector<EntityId> _vehicleHeadEntityList;
        bool _vehicleHeadEntityListDirty{ true };
        EntityIdList _freeIds;

        bool _entityFlashingList[kMaxEntities];

        std::array<std::vector<EntityId>, kSpatialIndexSize> gEntitySpatialIndex;
        std::vector<EntityId> _spatialIndexDirtyEntities;
        std::bitset<kMaxEntities> _spatialIndexDirtyQueued;

        std::array<uint32_t, kMaxEntities> _entityVisualGenerations{};
        std::array<uint8_t, kMaxEntities> _entityVisualDirtyFlags{};
        std::vector<EntityId> _entityVisualDirtyEntities;
        std::bitset<kMaxEntities> _entityVisualDirtyQueued;
        uint64_t _entityVisualEpoch{ 1 };
        bool _entityVisualResetPending{ true };

        template<typename T>
        static T* CastEntity(EntityBase* entity)
        {
            if constexpr (std::is_same_v<T, EntityBase>)
                return entity;
            else if constexpr (requires { T::kEntityType; })
                return entity != nullptr && entity->type == T::kEntityType ? entity->cast<T>() : nullptr;
            else
                return entity == nullptr ? nullptr : entity->as<T>();
        }

    public:
        EntityRegistry();
        ~EntityRegistry();

        EntityRegistry(const EntityRegistry&) = delete;
        EntityRegistry& operator=(const EntityRegistry&) = delete;

        uint16_t getEntityListCount(EntityType type);
        uint16_t getNumFreeEntities();

        EntityBase* getEntity(EntityId entityId);

        template<typename T>
        T* getEntity(EntityId entityId)
        {
            return CastEntity<T>(getEntity(entityId));
        }

        EntityBase* tryGetEntity(EntityId entityId)
        {
            const auto index = entityId.ToUnderlying();
            return index < kMaxEntities ? entities[index] : nullptr;
        }

        template<typename T>
        T* tryGetEntity(EntityId entityId)
        {
            return CastEntity<T>(tryGetEntity(entityId));
        }

        const std::vector<EntityId>& getEntityTileList(const CoordsXY& spritePos);

        EntityBase* createEntity(EntityType type);

        template<typename T>
        T* createEntity()
        {
            return static_cast<T*>(createEntity(T::kEntityType));
        }

        // Use only with imports that must happen at a specified index
        EntityBase* createEntityAt(EntityId index, EntityType type);
        // Use only with imports that must happen at a specified index
        template<typename T>
        T* createEntityAt(EntityId index)
        {
            return static_cast<T*>(createEntityAt(index, T::kEntityType));
        }

        const EntityIdList& getEntityList(EntityType id);
        const std::vector<EntityBase*>& GetEntityExecutionList(EntityType id) const noexcept;
        const std::vector<EntityId>& GetVehicleHeadEntityList();
        uint16_t getMiscEntityCount();

        void resetAllEntities();
        void resetEntitySpatialIndices();

        [[nodiscard]] EntityVisualHandle GetEntityVisualHandle(EntityId id) const noexcept;
        // A family filter retains identity/tombstone metadata for every dirty slot, but copies concrete payload only
        // for that family. null preserves the existing all-family capture. One publication owner consumes this queue.
        [[nodiscard]] EntityVisualChangeBatch ConsumeEntityVisualChanges(EntityType payloadFamily = EntityType::null);
        // Alternative mode of the same single-owner dirty stream, never a second consumer. Call only at an
        // authoritative completed-state boundary before tweening, with catalog generations indexed by object slot.
        // It captures only dirty peep field groups, not whole records. Invalid capture leaves notifications pending.
        // Starting after another mode consumed the reset requires explicit bootstrap by the publication owner.
        // Appearance mutation coverage and object-catalog lifetime must be qualified before renderer admission.
        // Two-phase shared intake. The same authoritative owner barrier must span prepare and acknowledgement.
        // No mutation may intervene; this is not a second dirty-stream subscriber.
        [[nodiscard]] Drawing::RetainedEntityPublicationInput CaptureRetainedEntityPublication(
            uint32_t sourceTick, std::span<const uint32_t> loadedObjectGenerations, bool bootstrap,
            bool includeBalloonCompatibility = true) const;
        void AcknowledgeRetainedEntityPublication();
        [[nodiscard]] uint64_t GetEntityVisualEpoch() const noexcept
        {
            return _entityVisualEpoch;
        }
        void PublishEntityVisualState(EntityBase& entity, EntityVisualDirty dirty = EntityVisualDirty::full) noexcept;
        void CaptureEntityPresentationStorage(EntityPresentationSnapshot& snapshot) const;

#ifndef DISABLE_NETWORK

        template<typename T>
        void networkSerialseEntityType(DataSerialiser& ds)
        {
            for (auto* ent : EntityList<T>())
            {
                ent->serialise(ds);
            }
        }

        template<typename... T>
        void networkSerialiseEntityTypes(DataSerialiser& ds)
        {
            (networkSerialseEntityType<T>(ds), ...);
        }

#endif // DISABLE_NETWORK

        EntitiesChecksum getAllEntitiesChecksum();

        template<typename T>
        void miscUpdateAllType()
        {
            for (auto misc : EntityList<T>())
            {
                misc->update();
            }
        }

        template<typename... T>
        void miscUpdateAllTypes()
        {
            (miscUpdateAllType<T>(), ...);
        }

        void updateAllMiscEntities();
        void updateMoneyEffect();
        void entityRemove(EntityBase* entity);
        uint16_t removeFloatingEntities();
        void updateEntitiesSpatialIndex();
        void updateEntitySpatialIndex(EntityBase& entity);

        void entitySetFlashing(EntityBase* entity, bool flashing);
        bool entityGetFlashing(EntityBase* entity);

    private:
        static constexpr std::array kMiscEntityTypes{
            EntityType::steamParticle,   EntityType::moneyEffect, EntityType::crashedVehicleParticle,
            EntityType::explosionCloud,  EntityType::crashSplash, EntityType::explosionFlare,
            EntityType::jumpingFountain, EntityType::balloon,     EntityType::duck,
        };

        static uint32_t ComputeSpatialIndex(const CoordsXY& location) noexcept;
        static uint32_t GetSpatialIndex(const EntityBase& entity) noexcept;
        static bool IsMiscEntity(EntityType type) noexcept;

        void prepareNewEntity(EntityBase& base, EntityType type);
        void entitySpatialInsert(EntityBase& entity, const CoordsXY& newLoc);
        void entitySpatialRemove(EntityBase& entity);
        void QueueEntitySpatialIndexUpdate(EntityBase& entity);
        void CancelEntitySpatialIndexUpdate(EntityBase& entity) noexcept;
        void ClearSpatialIndexDirtyWorklist() noexcept;
        void QueueEntityVisualChange(EntityId id, EntityVisualDirty dirty) noexcept;
        void QueueEntityVisualChange(EntityBase& entity, EntityVisualDirty dirty) noexcept;
        void ResetEntityVisualLifecycle() noexcept;
        void freeEntity(EntityBase& entity);
    };

    // Presentation code resolves through the scoped immutable scene when one is active and through the live registry otherwise.
    [[nodiscard]] EntityBase* GetEntityForPresentation(EntityId id) noexcept;

    template<typename T>
    [[nodiscard]] T* GetEntityForPresentation(EntityId id) noexcept
    {
        auto* entity = GetEntityForPresentation(id);
        if constexpr (std::is_same_v<T, EntityBase>)
            return entity;
        else if constexpr (requires { T::kEntityType; })
            return entity != nullptr && entity->type == T::kEntityType ? entity->cast<T>() : nullptr;
        else
            return entity == nullptr ? nullptr : entity->as<T>();
    }

} // namespace OpenRCT2
