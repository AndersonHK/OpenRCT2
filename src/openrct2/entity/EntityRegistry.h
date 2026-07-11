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

#include <array>
#include <bit>
#include <bitset>
#include <cstddef>
#include <cstdint>
#include <iterator>
#include <string>
#include <type_traits>
#include <vector>

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

    // Ascending entity-id membership with no per-entity allocation. Iterators preselect the next numeric id before exposing
    // the current entity, matching the former list cursor: removing the current entity is safe, removals ahead are skipped,
    // and insertions before the preselected next id are not newly visited.
    class EntityIdList
    {
    private:
        friend class EntityRegistry;

        static constexpr size_t kBitsPerWord = 64;
        static constexpr size_t kWordCount = (kMaxEntities + kBitsPerWord - 1) / kBitsPerWord;
        static constexpr uint32_t kEndIndex = kMaxEntities;

        std::array<uint64_t, kWordCount> _membership{};
        uint32_t _size{};

        [[nodiscard]] uint32_t FindNext(uint32_t start) const noexcept
        {
            if (start >= kMaxEntities)
                return kEndIndex;

            auto wordIndex = static_cast<size_t>(start / kBitsPerWord);
            auto word = _membership[wordIndex] & (~uint64_t{ 0 } << (start % kBitsPerWord));
            for (;;)
            {
                if (word != 0)
                {
                    const auto index = static_cast<uint32_t>(
                        (wordIndex * kBitsPerWord) + static_cast<size_t>(std::countr_zero(word)));
                    return index < kMaxEntities ? index : kEndIndex;
                }
                wordIndex++;
                if (wordIndex >= kWordCount)
                    return kEndIndex;
                word = _membership[wordIndex];
            }
        }

        [[nodiscard]] bool Contains(uint32_t index) const noexcept
        {
            if (index >= kMaxEntities)
                return false;
            return (_membership[index / kBitsPerWord] & (uint64_t{ 1 } << (index % kBitsPerWord))) != 0;
        }

        bool insert(EntityId id) noexcept
        {
            const auto index = id.ToUnderlying();
            if (index >= kMaxEntities)
                return false;
            const auto mask = uint64_t{ 1 } << (index % kBitsPerWord);
            auto& word = _membership[index / kBitsPerWord];
            if (word & mask)
                return false;
            word |= mask;
            _size++;
            return true;
        }

        bool erase(EntityId id) noexcept
        {
            const auto index = id.ToUnderlying();
            if (index >= kMaxEntities)
                return false;
            const auto mask = uint64_t{ 1 } << (index % kBitsPerWord);
            auto& word = _membership[index / kBitsPerWord];
            if (!(word & mask))
                return false;
            word &= ~mask;
            _size--;
            return true;
        }

        void clear() noexcept
        {
            _membership.fill(0);
            _size = 0;
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
                // The next live id was already selected before the caller saw
                // the current entity. Re-search only if it was removed in the
                // meantime; this preserves mutation visibility without paying
                // a second bit scan for every ordinary element.
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
                // Lookahead is an iteration detail and may legitimately differ after mutation. Iterator identity is the
                // owning range and current position only.
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

    union Entity_t
    {
        uint8_t Pad00[0x200];
        EntityBase base;
        Entity_t()
            : Pad00()
        {
        }
    };

#pragma pack(push, 1)
    struct EntitiesChecksum
    {
        std::array<std::byte, 20> raw;

        std::string ToString() const;
    };
#pragma pack(pop)

    template<typename T>
    class EntityList;
    template<typename T>
    class EntityListIterator;

    class EntityRegistry
    {
    private:
        friend struct EntityBase;
        template<typename T>
        friend class EntityListIterator;

        Entity_t entities[kMaxEntities]{};
        std::array<EntityIdList, EnumValue(EntityType::count)> gEntityLists;
        std::vector<EntityId> _vehicleHeadEntityList;
        bool _vehicleHeadEntityListDirty{ true };
        std::vector<EntityId> _freeIdList;

        bool _entityFlashingList[kMaxEntities];

        std::array<std::vector<EntityId>, kSpatialIndexSize> gEntitySpatialIndex;
        std::vector<EntityId> _spatialIndexDirtyEntities;
        std::bitset<kMaxEntities> _spatialIndexDirtyQueued;

    public:
        uint16_t GetEntityListCount(EntityType type);
        uint16_t GetNumFreeEntities();

        EntityBase* GetEntity(EntityId entityId);

        template<typename T>
        T* GetEntity(EntityId entityId)
        {
            if (entityId.IsNull())
                return nullptr;

            auto* ent = &entities[entityId.ToUnderlying()].base;
            if constexpr (std::is_same_v<T, EntityBase>)
            {
                return ent;
            }
            else if constexpr (requires { T::cEntityType; })
            {
                return ent->type == T::cEntityType ? ent->cast<T>() : nullptr;
            }
            else
            {
                return ent->as<T>();
            }
        }

        EntityBase* TryGetEntity(EntityId entityId)
        {
            const auto index = entityId.ToUnderlying();
            return index < kMaxEntities ? &entities[index].base : nullptr;
        }

        template<typename T>
        T* TryGetEntity(EntityId entityId)
        {
            auto* ent = TryGetEntity(entityId);
            if (ent == nullptr)
            {
                return nullptr;
            }
            if constexpr (std::is_same_v<T, EntityBase>)
            {
                return ent;
            }
            else if constexpr (requires { T::cEntityType; })
            {
                return ent->type == T::cEntityType ? ent->cast<T>() : nullptr;
            }
            else
            {
                return ent->as<T>();
            }
        }

        const std::vector<EntityId>& GetEntityTileList(const CoordsXY& spritePos);

        EntityBase* CreateEntity(EntityType type);

        template<typename T>
        T* CreateEntity()
        {
            return static_cast<T*>(CreateEntity(T::cEntityType));
        }

        // Use only with imports that must happen at a specified index
        EntityBase* CreateEntityAt(EntityId index, EntityType type);
        // Use only with imports that must happen at a specified index
        template<typename T>
        T* CreateEntityAt(EntityId index)
        {
            return static_cast<T*>(CreateEntityAt(index, T::cEntityType));
        }

        const EntityIdList& GetEntityList(EntityType id);
        const std::vector<EntityId>& GetVehicleHeadEntityList();
        uint16_t GetMiscEntityCount();

        void ResetAllEntities();
        void ResetEntitySpatialIndices();

#ifndef DISABLE_NETWORK

        template<typename T>
        void NetworkSerialseEntityType(DataSerialiser& ds)
        {
            for (auto* ent : EntityList<T>())
            {
                ent->serialise(ds);
            }
        }

        template<typename... T>
        void NetworkSerialiseEntityTypes(DataSerialiser& ds)
        {
            (NetworkSerialseEntityType<T>(ds), ...);
        }

#endif // DISABLE_NETWORK

        EntitiesChecksum GetAllEntitiesChecksum();

        template<typename T>
        void MiscUpdateAllType()
        {
            for (auto misc : EntityList<T>())
            {
                misc->Update();
            }
        }

        template<typename... T>
        void MiscUpdateAllTypes()
        {
            (MiscUpdateAllType<T>(), ...);
        }

        void UpdateAllMiscEntities();
        void UpdateMoneyEffect();
        void EntityRemove(EntityBase* entity);
        uint16_t RemoveFloatingEntities();
        void UpdateEntitiesSpatialIndex();
        void UpdateEntitySpatialIndex(EntityBase& entity);

        void EntitySetFlashing(EntityBase* entity, bool flashing);
        bool EntityGetFlashing(EntityBase* entity);

    private:
        void ResetEntityLists();
        void ResetFreeIds();
        void EntityReset(EntityBase& entity);
        void AddToEntityList(EntityBase& entity);
        void AddToFreeList(EntityId index);
        void RemoveFromEntityList(EntityBase& entity);
        void PrepareNewEntity(EntityBase& base, EntityType type);
        void EntitySpatialInsert(EntityBase& entity, const CoordsXY& newLoc);
        void EntitySpatialRemove(EntityBase& entity);
        void QueueEntitySpatialIndexUpdate(EntityBase& entity);
        void ClearSpatialIndexDirtyWorklist() noexcept;
        void FreeEntity(EntityBase& entity);
    };

} // namespace OpenRCT2
