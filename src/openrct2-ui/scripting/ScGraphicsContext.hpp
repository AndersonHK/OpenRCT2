/*****************************************************************************
 * Copyright (c) 2014-2026 OpenRCT2 developers
 *
 * For a complete list of all authors, please refer to contributors.md
 * Interested in contributing? Visit https://github.com/OpenRCT2/OpenRCT2
 *
 * OpenRCT2 is licensed under the GNU General Public License version 3.
 *****************************************************************************/

#pragma once

#ifdef ENABLE_SCRIPTING

    #include "CustomImages.h"
    #include <algorithm>
    #include <exception>
    #include <functional>

    #include <openrct2/drawing/Drawing.String.h>
    #include <openrct2/drawing/Drawing.h>
    #include <openrct2/drawing/Line.h>
    #include <openrct2/drawing/Rectangle.h>
    #include <openrct2/drawing/RenderTarget.h>
    #include <openrct2/drawing/Text.h>
    #include <openrct2/scripting/ScriptEngine.h>
    #include <quickjs.h>

namespace OpenRCT2::Scripting
{
    class ScGraphicsContext;
    extern ScGraphicsContext gScGraphicsContext;
    class ScGraphicsContext final : public ScBase
    {
    private:
        struct GraphicsData
        {
            Drawing::RenderTarget _rt{};
            std::function<void(Drawing::RenderTarget&)> _prepare;
            std::function<void(std::exception_ptr)> _recordingFailed;
            std::function<bool(Drawing::RenderTarget&, ImageId, ScreenCoordsXY)> _orderedAlias;
            bool _valid{ true };

            std::optional<uint8_t> _colour{};
            std::optional<uint8_t> _secondaryColour{};
            std::optional<uint8_t> _tertiaryColour{};
            std::optional<uint8_t> _paletteId{};
            Drawing::PaletteIndex _stroke{};
            Drawing::PaletteIndex _fill{};
        };

    public:
        void Register(JSContext* ctx)
        {
            static constexpr JSCFunctionListEntry funcs[] = {
                JS_CGETSET_DEF("colour", ScGraphicsContext::colour_get, ScGraphicsContext::colour_set),
                JS_CGETSET_DEF(
                    "secondaryColour", ScGraphicsContext::secondaryColour_get, ScGraphicsContext::secondaryColour_set),
                JS_CGETSET_DEF("ternaryColour", ScGraphicsContext::tertiaryColour_get, ScGraphicsContext::tertiaryColour_set),
                JS_CGETSET_DEF("tertiaryColour", ScGraphicsContext::tertiaryColour_get, ScGraphicsContext::tertiaryColour_set),
                JS_CGETSET_DEF("paletteId", ScGraphicsContext::paletteId_get, ScGraphicsContext::paletteId_set),
                JS_CGETSET_DEF("fill", ScGraphicsContext::fill_get, ScGraphicsContext::fill_set),
                JS_CGETSET_DEF("stroke", ScGraphicsContext::stroke_get, ScGraphicsContext::stroke_set),
                JS_CGETSET_DEF("width", ScGraphicsContext::width_get, nullptr),
                JS_CGETSET_DEF("height", ScGraphicsContext::height_get, nullptr),

                JS_CFUNC_DEF("getImage", 1, ScGraphicsContext::getImage),
                JS_CFUNC_DEF("measureText", 1, ScGraphicsContext::measureText),

                JS_CFUNC_DEF("box", 4, ScGraphicsContext::box),
                JS_CFUNC_DEF("clear", 0, ScGraphicsContext::clear),
                JS_CFUNC_DEF("clip", 4, ScGraphicsContext::clip),
                JS_CFUNC_DEF("image", 3, ScGraphicsContext::image),
                JS_CFUNC_DEF("line", 4, ScGraphicsContext::lineJS),
                JS_CFUNC_DEF("rect", 4, ScGraphicsContext::rect),
                JS_CFUNC_DEF("text", 3, ScGraphicsContext::text),
                JS_CFUNC_DEF("well", 4, ScGraphicsContext::well),
            };
            RegisterBase(ctx, "GraphicsContext", Finalize, funcs);
        }

        static void Finalize(JSRuntime* rt, JSValue thisVal)
        {
            GraphicsData* data = gScGraphicsContext.GetOpaque<GraphicsData*>(thisVal);
            if (data)
                delete data;
        }

        JSValue New(
            JSContext* ctx, const Drawing::RenderTarget& rt,
            std::function<void(Drawing::RenderTarget&)> prepare = {},
            std::function<void(std::exception_ptr)> recordingFailed = {},
            std::function<bool(Drawing::RenderTarget&, ImageId, ScreenCoordsXY)> orderedAlias = {})
        {
            auto* data = new GraphicsData{};
            data->_rt = rt;
            data->_prepare = std::move(prepare);
            data->_recordingFailed = std::move(recordingFailed);
            data->_orderedAlias = std::move(orderedAlias);
            return MakeWithOpaque(ctx, data);
        }

        void Invalidate(JSValue value)
        {
            auto* data = GetOpaque<GraphicsData*>(value);
            data->_valid = false;
            data->_prepare = {};
            data->_recordingFailed = {};
            data->_orderedAlias = {};
            data->_rt.bits = nullptr;
            data->_rt.DrawingEngine = nullptr;
        }

    private:
        static JSValue RecordingError(JSContext* ctx, JSValue value, const std::exception& e)
        {
            auto* data = gScGraphicsContext.GetOpaque<GraphicsData*>(value);
            if (data->_recordingFailed)
                data->_recordingFailed(std::current_exception());
            return JS_ThrowInternalError(ctx, "%s", e.what());
        }
        static bool Prepare(JSContext* ctx, GraphicsData* data)
        {
            try
            {
                if (!data->_valid)
                    throw std::runtime_error("Graphics context is only valid during its drawing callback");
                if (data->_rt.width <= 0 || data->_rt.height <= 0)
                    return true;
                if (data->_prepare)
                    data->_prepare(data->_rt);
                return true;
            }
            catch (const std::exception& e)
            {
                JS_ThrowInternalError(ctx, "%s", e.what());
                return false;
            }
        }
        static JSValue colour_get(JSContext* ctx, JSValue thisVal)
        {
            GraphicsData* data = gScGraphicsContext.GetOpaque<GraphicsData*>(thisVal);
            return ToJSValue(ctx, data->_colour);
        }

        static JSValue colour_set(JSContext* ctx, JSValue thisVal, JSValue value)
        {
            GraphicsData* data = gScGraphicsContext.GetOpaque<GraphicsData*>(thisVal);
            if (JS_IsNumber(value))
                data->_colour = static_cast<uint8_t>(JSToInt(ctx, value));
            else
                data->_colour = {};
            return JS_UNDEFINED;
        }

        static JSValue secondaryColour_get(JSContext* ctx, JSValue thisVal)
        {
            GraphicsData* data = gScGraphicsContext.GetOpaque<GraphicsData*>(thisVal);
            return ToJSValue(ctx, data->_secondaryColour);
        }

        static JSValue secondaryColour_set(JSContext* ctx, JSValue thisVal, JSValue value)
        {
            GraphicsData* data = gScGraphicsContext.GetOpaque<GraphicsData*>(thisVal);
            if (JS_IsNumber(value))
                data->_secondaryColour = static_cast<uint8_t>(JSToInt(ctx, value));
            else
                data->_secondaryColour = {};
            return JS_UNDEFINED;
        }

        static JSValue tertiaryColour_get(JSContext* ctx, JSValue thisVal)
        {
            GraphicsData* data = gScGraphicsContext.GetOpaque<GraphicsData*>(thisVal);
            return ToJSValue(ctx, data->_tertiaryColour);
        }

        static JSValue tertiaryColour_set(JSContext* ctx, JSValue thisVal, JSValue value)
        {
            GraphicsData* data = gScGraphicsContext.GetOpaque<GraphicsData*>(thisVal);
            if (JS_IsNumber(value))
                data->_tertiaryColour = static_cast<uint8_t>(JSToInt(ctx, value));
            else
                data->_tertiaryColour = {};
            return JS_UNDEFINED;
        }

        static JSValue paletteId_get(JSContext* ctx, JSValue thisVal)
        {
            GraphicsData* data = gScGraphicsContext.GetOpaque<GraphicsData*>(thisVal);
            return ToJSValue(ctx, data->_paletteId);
        }

        static JSValue paletteId_set(JSContext* ctx, JSValue thisVal, JSValue value)
        {
            GraphicsData* data = gScGraphicsContext.GetOpaque<GraphicsData*>(thisVal);
            if (JS_IsNumber(value))
                data->_paletteId = static_cast<uint8_t>(JSToInt(ctx, value));
            else
                data->_paletteId = {};
            return JS_UNDEFINED;
        }

        static JSValue fill_get(JSContext* ctx, JSValue thisVal)
        {
            GraphicsData* data = gScGraphicsContext.GetOpaque<GraphicsData*>(thisVal);
            return JS_NewInt32(ctx, EnumValue(data->_fill));
        }

        static JSValue fill_set(JSContext* ctx, JSValue thisVal, JSValue value)
        {
            JS_UNPACK_INT32(valueInt, ctx, value)
            GraphicsData* data = gScGraphicsContext.GetOpaque<GraphicsData*>(thisVal);
            data->_fill = static_cast<Drawing::PaletteIndex>(valueInt);
            return JS_UNDEFINED;
        }

        static JSValue stroke_get(JSContext* ctx, JSValue thisVal)
        {
            GraphicsData* data = gScGraphicsContext.GetOpaque<GraphicsData*>(thisVal);
            return JS_NewInt32(ctx, EnumValue(data->_stroke));
        }

        static JSValue stroke_set(JSContext* ctx, JSValue thisVal, JSValue value)
        {
            JS_UNPACK_INT32(valueInt, ctx, value);
            GraphicsData* data = gScGraphicsContext.GetOpaque<GraphicsData*>(thisVal);
            data->_stroke = static_cast<Drawing::PaletteIndex>(valueInt);
            return JS_UNDEFINED;
        }

        static JSValue width_get(JSContext* ctx, JSValue thisVal)
        {
            GraphicsData* data = gScGraphicsContext.GetOpaque<GraphicsData*>(thisVal);
            return JS_NewInt32(ctx, data->_rt.width);
        }

        static JSValue height_get(JSContext* ctx, JSValue thisVal)
        {
            GraphicsData* data = gScGraphicsContext.GetOpaque<GraphicsData*>(thisVal);
            return JS_NewInt32(ctx, data->_rt.height);
        }

        static JSValue getImage(JSContext* ctx, JSValue thisVal, int argc, JSValue* argv)
        {
            JS_UNPACK_UINT32(id, ctx, argv[0]);

            return JSGetImageInfo(ctx, id);
        }

        static JSValue measureText(JSContext* ctx, JSValue thisVal, int argc, JSValue* argv)
        {
            JS_UNPACK_STR(text, ctx, argv[0])

            auto width = Drawing::getStringWidth(text, FontStyle::medium);
            auto height = Drawing::getStringHeightRaw(text.c_str(), FontStyle::medium);
            return ToJSValue(ctx, ScreenSize{ width, height });
        }

        static JSValue box(JSContext* ctx, JSValue thisVal, int argc, JSValue* argv)
        {
            try
            {
                JS_UNPACK_INT32(x, ctx, argv[0]);
                JS_UNPACK_INT32(y, ctx, argv[1]);
                JS_UNPACK_INT32(width, ctx, argv[2]);
                JS_UNPACK_INT32(height, ctx, argv[3]);
                GraphicsData* data = gScGraphicsContext.GetOpaque<GraphicsData*>(thisVal);
                if (!Prepare(ctx, data))
                    return JS_EXCEPTION;
                if (data->_rt.width <= 0 || data->_rt.height <= 0)
                    return JS_UNDEFINED;

                Drawing::Rectangle::fillInset(
                    data->_rt, { x, y, x + width - 1, y + height - 1 },
                    { static_cast<Drawing::Colour>(data->_colour.value_or(0)) });
                return JS_UNDEFINED;
            }
            catch (const std::exception& e)
            {
                return RecordingError(ctx, thisVal, e);
            }
        }

        static JSValue well(JSContext* ctx, JSValue thisVal, int argc, JSValue* argv)
        {
            try
            {
                JS_UNPACK_INT32(x, ctx, argv[0]);
                JS_UNPACK_INT32(y, ctx, argv[1]);
                JS_UNPACK_INT32(width, ctx, argv[2]);
                JS_UNPACK_INT32(height, ctx, argv[3]);
                GraphicsData* data = gScGraphicsContext.GetOpaque<GraphicsData*>(thisVal);
                if (!Prepare(ctx, data))
                    return JS_EXCEPTION;
                if (data->_rt.width <= 0 || data->_rt.height <= 0)
                    return JS_UNDEFINED;

                Drawing::Rectangle::fillInset(
                    data->_rt, { x, y, x + width - 1, y + height - 1 }, { static_cast<Drawing::Colour>(data->_colour.value_or(0)) },
                    Drawing::Rectangle::BorderStyle::inset, Drawing::Rectangle::FillBrightness::light,
                    Drawing::Rectangle::FillMode::dontLightenWhenInset);
                return JS_UNDEFINED;
            }
            catch (const std::exception& e)
            {
                return RecordingError(ctx, thisVal, e);
            }
        }

        static JSValue clear(JSContext* ctx, JSValue thisVal, int argc, JSValue* argv)
        {
            try
            {
                GraphicsData* data = gScGraphicsContext.GetOpaque<GraphicsData*>(thisVal);
                if (!Prepare(ctx, data))
                    return JS_EXCEPTION;
                if (data->_rt.width <= 0 || data->_rt.height <= 0)
                    return JS_UNDEFINED;
                GfxClear(data->_rt, data->_fill);
                return JS_UNDEFINED;
            }
            catch (const std::exception& e)
            {
                return RecordingError(ctx, thisVal, e);
            }
        }

        static JSValue clip(JSContext* ctx, JSValue thisVal, int argc, JSValue* argv)
        {
            try
            {
                JS_UNPACK_INT32(x, ctx, argv[0]);
                JS_UNPACK_INT32(y, ctx, argv[1]);
                JS_UNPACK_INT32(width, ctx, argv[2]);
                JS_UNPACK_INT32(height, ctx, argv[3]);
                GraphicsData* data = gScGraphicsContext.GetOpaque<GraphicsData*>(thisVal);
                if (!Prepare(ctx, data))
                    return JS_EXCEPTION;

                auto& rt = data->_rt;
                // Intersect in wide integers before forming a pixel pointer. The generic helper may move bits
                // outside its allocation before reporting an empty intersection.
                const int64_t left = std::max<int64_t>(rt.x, x);
                const int64_t top = std::max<int64_t>(rt.y, y);
                const int64_t right = std::min<int64_t>(int64_t{ rt.x } + rt.width, int64_t{ x } + width);
                const int64_t bottom = std::min<int64_t>(int64_t{ rt.y } + rt.height, int64_t{ y } + height);
                const auto stride = rt.LineStride();
                if (rt.width <= 0 || rt.height <= 0 || width <= 0 || height <= 0 || left >= right || top >= bottom)
                {
                    // Empty clips remain empty across nested callbacks. Keep a safe pointer; drawing operations
                    // return before recording, resolving sources or touching this target.
                    rt.x = 0;
                    rt.y = 0;
                    rt.width = 0;
                    rt.height = 0;
                    rt.pitch = stride;
                    return JS_UNDEFINED;
                }
                const auto offset = (top - rt.y) * stride + (left - rt.x);
                rt.bits += static_cast<ptrdiff_t>(offset);
                rt.x = static_cast<int32_t>(left - x);
                rt.y = static_cast<int32_t>(top - y);
                rt.width = static_cast<int32_t>(right - left);
                rt.height = static_cast<int32_t>(bottom - top);
                rt.pitch = stride - rt.width;
                return JS_UNDEFINED;
            }
            catch (const std::exception& e)
            {
                return RecordingError(ctx, thisVal, e);
            }
        }

        static JSValue image(JSContext* ctx, JSValue thisVal, int argc, JSValue* argv)
        {
            try
            {
                JS_UNPACK_UINT32(id, ctx, argv[0]);
                JS_UNPACK_INT32(x, ctx, argv[1]);
                JS_UNPACK_INT32(y, ctx, argv[2]);
                GraphicsData* data = gScGraphicsContext.GetOpaque<GraphicsData*>(thisVal);
                if (!data->_valid)
                    throw std::runtime_error("Graphics context is only valid during its drawing callback");
                if (data->_rt.width <= 0 || data->_rt.height <= 0)
                    return JS_UNDEFINED;
                ImageId img;
                img = img.WithIndex(id);
                if (data->_paletteId)
                {
                    img = img.WithRemap(*data->_paletteId);
                }
                else
                {
                    if (data->_colour)
                    {
                        img = img.WithPrimary(static_cast<Drawing::Colour>(*data->_colour));
                    }
                    if (data->_secondaryColour)
                    {
                        img = img.WithSecondary(static_cast<Drawing::Colour>(*data->_secondaryColour));
                    }
                }

                img = img.WithTertiary(static_cast<Drawing::Colour>(data->_tertiaryColour.value_or(0)));
                if (data->_orderedAlias && data->_orderedAlias(data->_rt, img, { x, y }))
                    return JS_UNDEFINED;
                PrepareCustomImageSource(id);
                if (!Prepare(ctx, data))
                    return JS_EXCEPTION;
                GfxDrawSprite(data->_rt, img, { x, y });
                return JS_UNDEFINED;
            }
            catch (const std::exception& e)
            {
                return RecordingError(ctx, thisVal, e);
            }
        }

        static void line(GraphicsData* data, int32_t x1, int32_t y1, int32_t x2, int32_t y2)
        {
            GfxDrawLine(data->_rt, { { x1, y1 }, { x2, y2 } }, data->_stroke);
        }

        static JSValue lineJS(JSContext* ctx, JSValue thisVal, int argc, JSValue* argv)
        {
            try
            {
                JS_UNPACK_INT32(x1, ctx, argv[0]);
                JS_UNPACK_INT32(y1, ctx, argv[1]);
                JS_UNPACK_INT32(x2, ctx, argv[2]);
                JS_UNPACK_INT32(y2, ctx, argv[3]);
                GraphicsData* data = gScGraphicsContext.GetOpaque<GraphicsData*>(thisVal);
                if (!Prepare(ctx, data))
                    return JS_EXCEPTION;
                if (data->_rt.width <= 0 || data->_rt.height <= 0)
                    return JS_UNDEFINED;
                line(data, x1, y1, x2, y2);
                return JS_UNDEFINED;
            }
            catch (const std::exception& e)
            {
                return RecordingError(ctx, thisVal, e);
            }
        }

        static JSValue rect(JSContext* ctx, JSValue thisVal, int argc, JSValue* argv)
        {
            try
            {
                JS_UNPACK_INT32(x, ctx, argv[0]);
                JS_UNPACK_INT32(y, ctx, argv[1]);
                JS_UNPACK_INT32(width, ctx, argv[2]);
                JS_UNPACK_INT32(height, ctx, argv[3]);
                GraphicsData* data = gScGraphicsContext.GetOpaque<GraphicsData*>(thisVal);
                if (!Prepare(ctx, data))
                    return JS_EXCEPTION;
                if (data->_rt.width <= 0 || data->_rt.height <= 0)
                    return JS_UNDEFINED;

                if (data->_stroke != Drawing::PaletteIndex::transparent)
                {
                    line(data, x, y, x + width, y);
                    line(data, x + width - 1, y + 1, x + width - 1, y + height - 1);
                    line(data, x, y + height - 1, x + width, y + height - 1);
                    line(data, x, y + 1, x, y + height - 1);

                    x++;
                    y++;
                    width -= 2;
                    height -= 2;
                }
                if (data->_fill != Drawing::PaletteIndex::transparent)
                {
                    Drawing::Rectangle::fill(data->_rt, { x, y, x + width - 1, y + height - 1 }, data->_fill);
                }
                return JS_UNDEFINED;
            }
            catch (const std::exception& e)
            {
                return RecordingError(ctx, thisVal, e);
            }
        }

        static JSValue text(JSContext* ctx, JSValue thisVal, int argc, JSValue* argv)
        {
            try
            {
                JS_UNPACK_STR(text, ctx, argv[0]);
                JS_UNPACK_INT32(x, ctx, argv[1]);
                JS_UNPACK_INT32(y, ctx, argv[2]);
                GraphicsData* data = gScGraphicsContext.GetOpaque<GraphicsData*>(thisVal);
                if (!Prepare(ctx, data))
                    return JS_EXCEPTION;
                if (data->_rt.width <= 0 || data->_rt.height <= 0)
                    return JS_UNDEFINED;
                drawText(data->_rt, { x, y }, text, { static_cast<Drawing::Colour>(data->_colour.value_or(0)) });
                return JS_UNDEFINED;
            }
            catch (const std::exception& e)
            {
                return RecordingError(ctx, thisVal, e);
            }
        }
    };
} // namespace OpenRCT2::Scripting

#endif
