/*****************************************************************************
 * Copyright (c) 2014-2026 OpenRCT2 developers
 *
 * For a complete list of all authors, please refer to contributors.md
 * Interested in contributing? Visit https://github.com/OpenRCT2/OpenRCT2
 *
 * OpenRCT2 is licensed under the GNU General Public License version 3.
 *****************************************************************************/

#ifdef ENABLE_SCRIPTING

    #include "ScTile.hpp"

    #include "../../../Context.h"
    #include "../../../core/Guard.hpp"
    #include "../../../drawing/ScrollingText.h"
    #include "../../../entity/EntityRegistry.h"
    #include "../../../object/LargeSceneryEntry.h"
    #include "../../../world/Footpath.h"
    #include "../../../world/Map.h"
    #include "../../../world/MapTopology.h"
    #include "../../../world/Scenery.h"
    #include "../../../world/tile_element/LargeSceneryElement.h"
    #include "../../ScriptEngine.h"
    #include "ScTileElement.hpp"

    #include <cstdio>
    #include <cstring>
    #include <utility>

namespace OpenRCT2::Scripting
{
    using OpaqueTileData = struct
    {
        CoordsXY coords;
    };

    JSValue ScTile::x_get(JSContext* ctx, JSValue thisValue)
    {
        auto coords = GetCoordinates(thisValue);
        return JS_NewInt32(ctx, coords.x / kCoordsXYStep);
    }

    JSValue ScTile::y_get(JSContext* ctx, JSValue thisValue)
    {
        auto coords = GetCoordinates(thisValue);
        return JS_NewInt32(ctx, coords.y / kCoordsXYStep);
    }

    JSValue ScTile::numElements_get(JSContext* ctx, JSValue thisValue)
    {
        auto first = GetFirstElement(thisValue);
        return JS_NewUint32(ctx, GetNumElements(first));
    }

    JSValue ScTile::elements_get(JSContext* ctx, JSValue thisValue)
    {
        auto array = JS_NewArray(ctx);
        auto coords = GetCoordinates(thisValue);
        auto first = MapGetFirstElementAt(coords);
        auto currentNumElements = GetNumElements(first);
        if (currentNumElements != 0)
        {
            JS_SetLength(ctx, array, currentNumElements);
            for (size_t i = 0; i < currentNumElements; i++)
            {
                JS_SetPropertyInt64(ctx, array, i, gScTileElement.New(ctx, &first[i], coords));
            }
        }
        return array;
    }

    JSValue ScTile::data_get(JSContext* ctx, JSValue thisValue)
    {
        auto first = GetFirstElement(thisValue);
        auto dataLen = GetNumElements(first) * sizeof(TileElement);
        if (first != nullptr)
        {
            return JS_NewUint8ArrayCopy(ctx, reinterpret_cast<const uint8_t*>(first), dataLen);
        }
        return JS_NewUint8ArrayCopy(ctx, nullptr, 0);
    }

    JSValue ScTile::data_set(JSContext* ctx, JSValue thisValue, JSValue jsValue)
    {
        JS_THROW_IF_GAME_STATE_NOT_MUTABLE();
        if (JS_GetTypedArrayType(jsValue) == JSTypedArrayEnum::JS_TYPED_ARRAY_UINT8)
        {
            auto coords = GetCoordinates(thisValue);
            int64_t dataLength{};
            JS_GetLength(ctx, jsValue, &dataLength);
            auto dataSize = static_cast<size_t>(dataLength);
            auto* array = JS_GetUint8Array(ctx, &dataSize, jsValue);
            if (dataLength <= 0 || dataLength % sizeof(TileElement) != 0)
            {
                JS_ThrowPlainError(ctx, "Tile data must contain a non-empty whole number of tile elements.");
                return JS_EXCEPTION;
            }

            const auto numElements = static_cast<size_t>(dataLength) / sizeof(TileElement);
            std::vector<TileElement> elements(numElements);
            std::memcpy(elements.data(), array, numElements * sizeof(TileElement));
            const auto status = ReplaceTileElementsAt(TileCoordsXY{ coords }, std::move(elements));
            if (status != TileMutationStatus::ok)
            {
                JS_ThrowPlainError(ctx, "Tile data must leave the tile non-empty and contain a surface element.");
                return JS_EXCEPTION;
            }
        }
        return JS_UNDEFINED;
    }

    JSValue ScTile::getElement(JSContext* ctx, JSValue thisValue, int argc, JSValue* argv)
    {
        JS_UNPACK_UINT32(index, ctx, argv[0]);
        auto coords = GetCoordinates(thisValue);
        auto first = MapGetFirstElementAt(coords);
        if (static_cast<size_t>(index) < GetNumElements(first))
        {
            return gScTileElement.New(ctx, &first[index], coords);
        }
        return JS_UNDEFINED;
    }

    JSValue ScTile::insertElement(JSContext* ctx, JSValue thisValue, int argc, JSValue* argv)
    {
        JS_THROW_IF_GAME_STATE_NOT_MUTABLE();
        JS_UNPACK_UINT32(index, ctx, argv[0]);
        auto coords = GetCoordinates(thisValue);
        auto first = MapGetFirstElementAt(coords);
        auto origNumElements = GetNumElements(first);
        if (index <= origNumElements)
        {
            std::vector<TileElement> data(first, first + origNumElements);

            TileElement newElement{};
            newElement.ClearAs(TileElementType::surface);
            data.insert(data.begin() + index, newElement);
            if (ReplaceTileElementsAt(TileCoordsXY{ coords }, std::move(data)) != TileMutationStatus::ok)
            {
                JS_ThrowPlainError(ctx, "Unable to allocate element.");
                return JS_EXCEPTION;
            }
            first = MapGetFirstElementAt(coords);
            return gScTileElement.New(ctx, &first[index], coords);
        }
        else
        {
            JS_ThrowPlainError(ctx, "Index must be between zero and the number of elements on the tile.");
            return JS_EXCEPTION;
        }
    }

    JSValue ScTile::removeElement(JSContext* ctx, JSValue thisValue, int argc, JSValue* argv)
    {
        JS_THROW_IF_GAME_STATE_NOT_MUTABLE();
        JS_UNPACK_UINT32(index, ctx, argv[0]);
        auto coords = GetCoordinates(thisValue);
        auto first = MapGetFirstElementAt(coords);
        if (index < GetNumElements(first))
        {
            auto* element = &first[index];
            TileElement removedElement = *element;
            const bool removeBanner = element->getType() != TileElementType::largeScenery
                || element->asLargeScenery()->GetEntry()->scrolling_mode == kScrollingModeNone
                || ScTileElement::GetOtherLargeSceneryElement(coords, element->asLargeScenery()) == nullptr;

            std::vector<TileElement> elements(first, first + GetNumElements(first));
            elements.erase(elements.begin() + index);
            if (ReplaceTileElementsAt(TileCoordsXY{ coords }, std::move(elements)) != TileMutationStatus::ok)
            {
                JS_ThrowPlainError(ctx, "A tile must remain non-empty and contain a surface element.");
                return JS_EXCEPTION;
            }
            if (removeBanner)
                removedElement.RemoveBannerEntry();
        }
        return JS_UNDEFINED;
    }

    CoordsXY ScTile::GetCoordinates(JSValue thisValue)
    {
        return gScTile.GetOpaque<OpaqueTileData*>(thisValue)->coords;
    }

    TileElement* ScTile::GetFirstElement(JSValue thisValue)
    {
        auto coords = GetCoordinates(thisValue);
        return MapGetFirstElementAt(coords);
    }

    uint32_t ScTile::GetNumElements(const TileElement* first)
    {
        uint32_t count = 0;
        if (first != nullptr)
        {
            auto element = first;
            do
            {
                count++;
            } while (!(element++)->isLastForTile());
        }
        return count;
    }

    JSValue ScTile::New(JSContext* ctx, CoordsXY& coords)
    {
        return MakeWithOpaque(ctx, new OpaqueTileData{ coords });
    }

    void ScTile::Register(JSContext* ctx)
    {
        static constexpr JSCFunctionListEntry funcs[] = { JS_CGETSET_DEF("x", ScTile::x_get, nullptr),
                                                          JS_CGETSET_DEF("y", ScTile::y_get, nullptr),
                                                          JS_CGETSET_DEF("elements", ScTile::elements_get, nullptr),
                                                          JS_CGETSET_DEF("numElements", ScTile::numElements_get, nullptr),
                                                          JS_CGETSET_DEF("data", ScTile::data_get, ScTile::data_set),
                                                          JS_CFUNC_DEF("getElement", 1, ScTile::getElement),
                                                          JS_CFUNC_DEF("insertElement", 1, ScTile::insertElement),
                                                          JS_CFUNC_DEF("removeElement", 0, ScTile::removeElement) };
        RegisterBase(ctx, "Tile", Finalize, funcs);
    }

    void ScTile::Finalize(JSRuntime* rt, JSValue thisVal)
    {
        OpaqueTileData* data = gScTile.GetOpaque<OpaqueTileData*>(thisVal);
        if (data)
            delete data;
    }

} // namespace OpenRCT2::Scripting

#endif
