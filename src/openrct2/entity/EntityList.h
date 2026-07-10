/*****************************************************************************
 * Copyright (c) 2014-2026 OpenRCT2 developers
 *
 * For a complete list of all authors, please refer to contributors.md
 * Interested in contributing? Visit https://github.com/OpenRCT2/OpenRCT2
 *
 * OpenRCT2 is licensed under the GNU General Public License version 3.
 *****************************************************************************/

#pragma once

#include "../GameState.h"
#include "../rct12/RCT12.h"
#include "../world/Location.hpp"
#include "EntityBase.h"
#include "EntityRegistry.h"

#include <list>
#include <type_traits>
#include <vector>

namespace OpenRCT2
{
    template<typename T>
    class EntityTileIterator
    {
    private:
        EntityRegistry* registry;
        std::vector<EntityId>::const_iterator iter;
        std::vector<EntityId>::const_iterator end;
        T* Entity = nullptr;

    public:
        EntityTileIterator(
            EntityRegistry& _registry, std::vector<EntityId>::const_iterator _iter, std::vector<EntityId>::const_iterator _end)
            : registry(&_registry)
            , iter(_iter)
            , end(_end)
        {
            ++(*this);
        }
        EntityTileIterator& operator++()
        {
            Entity = nullptr;

            while (iter != end && Entity == nullptr)
            {
                auto* entity = registry->TryGetEntity(*iter++);
                if constexpr (std::is_same_v<T, EntityBase>)
                {
                    Entity = entity;
                }
                else if constexpr (requires { T::cEntityType; })
                {
                    if (entity != nullptr && entity->type == T::cEntityType)
                    {
                        Entity = entity->cast<T>();
                    }
                }
                else if (entity != nullptr)
                {
                    Entity = entity->as<T>();
                }
            }
            return *this;
        }

        EntityTileIterator operator++(int)
        {
            EntityTileIterator retval = *this;
            ++(*this);
            return retval;
        }
        bool operator==(EntityTileIterator other) const
        {
            return Entity == other.Entity;
        }
        bool operator!=(EntityTileIterator other) const
        {
            return !(*this == other);
        }
        T* operator*()
        {
            return Entity;
        }
        // iterator traits
        using difference_type = std::ptrdiff_t;
        using value_type = T;
        using pointer = const T*;
        using reference = const T&;
        using iterator_category = std::forward_iterator_tag;
    };

    template<typename T = EntityBase>
    class EntityTileList
    {
    private:
        EntityRegistry& registry;
        const std::vector<EntityId>& vec;

    public:
        EntityTileList(const CoordsXY& loc)
            : registry(getGameState().entities)
            , vec(registry.GetEntityTileList(loc))
        {
        }

        EntityTileIterator<T> begin()
        {
            return EntityTileIterator<T>(registry, std::begin(vec), std::end(vec));
        }
        EntityTileIterator<T> end()
        {
            return EntityTileIterator<T>(registry, std::end(vec), std::end(vec));
        }
    };

    template<typename T>
    class EntityListIterator
    {
    private:
        EntityRegistry* registry;
        std::list<EntityId>::const_iterator iter;
        std::list<EntityId>::const_iterator end;
        T* Entity = nullptr;

    public:
        EntityListIterator(
            EntityRegistry& _registry, std::list<EntityId>::const_iterator _iter, std::list<EntityId>::const_iterator _end)
            : registry(&_registry)
            , iter(_iter)
            , end(_end)
        {
            ++(*this);
        }
        EntityListIterator& operator++()
        {
            Entity = nullptr;

            while (iter != end && Entity == nullptr)
            {
                auto* entity = registry->TryGetEntity(*iter++);
                if constexpr (std::is_same_v<T, EntityBase>)
                {
                    Entity = entity;
                }
                else if constexpr (requires { T::cEntityType; })
                {
                    if (entity != nullptr && entity->type == T::cEntityType)
                    {
                        // The typed entity lists already guarantee the concrete type.
                        // Avoid repeating EntityBase::is<T>() for every hot-list step.
                        Entity = entity->cast<T>();
                    }
                }
                else if (entity != nullptr)
                {
                    Entity = entity->as<T>();
                }
            }
            return *this;
        }

        EntityListIterator operator++(int)
        {
            EntityListIterator retval = *this;
            ++(*this);
            return retval;
        }
        bool operator==(EntityListIterator other) const
        {
            return Entity == other.Entity;
        }
        bool operator!=(EntityListIterator other) const
        {
            return !(*this == other);
        }
        T* operator*()
        {
            return Entity;
        }
        // iterator traits
        using difference_type = std::ptrdiff_t;
        using value_type = T;
        using pointer = const T*;
        using reference = const T&;
        using iterator_category = std::forward_iterator_tag;
    };

    template<typename T = EntityBase>
    class EntityList
    {
    private:
        using EntityListIterator_t = EntityListIterator<T>;
        EntityRegistry& registry;
        const std::list<EntityId>& vec;

    public:
        EntityList()
            : registry(getGameState().entities)
            , vec(registry.GetEntityList(T::cEntityType))
        {
        }

        EntityListIterator_t begin() const
        {
            return EntityListIterator_t(registry, std::cbegin(vec), std::cend(vec));
        }
        EntityListIterator_t end() const
        {
            return EntityListIterator_t(registry, std::cend(vec), std::cend(vec));
        }
    };
} // namespace OpenRCT2
