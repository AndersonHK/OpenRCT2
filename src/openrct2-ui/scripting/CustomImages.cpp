/*****************************************************************************
 * Copyright (c) 2014-2026 OpenRCT2 developers
 *
 * For a complete list of all authors, please refer to contributors.md
 * Interested in contributing? Visit https://github.com/OpenRCT2/OpenRCT2
 *
 * OpenRCT2 is licensed under the GNU General Public License version 3.
 *****************************************************************************/

#ifdef ENABLE_SCRIPTING

    #include "CustomImages.h"

    #include "ScGraphicsContext.hpp"

    #include <openrct2/Context.h>
    #include <openrct2/drawing/Drawing.Sprite.h>
    #include <openrct2/drawing/Image.h>
    #include <openrct2/drawing/ImageImporter.h>
    #include <openrct2/drawing/NewDrawing.h>
    #include <openrct2/drawing/Palette.h>
    #include <openrct2/drawing/RenderService.h>
    #include <limits>
    #include <thirdparty/base64.hpp>

using namespace OpenRCT2::Drawing;

namespace OpenRCT2::Scripting
{
    enum class PixelDataKind
    {
        unknown,
        raw,
        rle,
        palette,
        png
    };

    enum class PixelDataPaletteKind
    {
        none,
        keep,
        closest,
        dither
    };

    struct PixelData
    {
        PixelDataKind Type;
        int32_t Width;
        int32_t Height;
        int32_t Stride;
        PixelDataPaletteKind Palette;
        JSValue Data;
    };

    struct AllocatedImageList
    {
        std::shared_ptr<Plugin> Owner;
        ImageList Range;
    };

    static void FlushCustomImageDrawing();
    static std::vector<AllocatedImageList> _allocatedImages;
    // Custom image callbacks are synchronous. Keep the destination stable until its owned result is committed.
    static std::vector<ImageIndex> _drawingImages;

    static bool IsDrawingImage(ImageIndex id)
    {
        return std::find(_drawingImages.begin(), _drawingImages.end(), id) != _drawingImages.end();
    }

    static void FreeImages(ImageList range)
    {
        FlushCustomImageDrawing();
        for (ImageIndex i = 0; i < range.Count; i++)
        {
            auto index = range.BaseId + i;
            auto g1 = GfxGetG1Element(index);
            if (g1 != nullptr)
            {
                // Free pixel data
                delete[] g1->offset;

                // Replace slot with empty element
                G1Element empty{};
                GfxSetG1Element(index, &empty);
                DrawingEngineInvalidateImage(index);
            }
        }
        GfxObjectFreeImages(range.BaseId, range.Count);
    }

    std::optional<ImageList> AllocateCustomImages(const std::shared_ptr<Plugin>& plugin, uint32_t count)
    {
        FlushCustomImageDrawing();
        std::vector<G1Element> images;
        images.resize(count);

        auto base = GfxObjectAllocateImages(images.data(), count);
        if (base == kImageIndexUndefined)
        {
            return {};
        }
        auto range = ImageList(base, count);

        AllocatedImageList item;
        item.Owner = plugin;
        item.Range = range;
        _allocatedImages.push_back(std::move(item));
        return range;
    }

    bool FreeCustomImages(const std::shared_ptr<Plugin>& plugin, ImageList range)
    {
        if (std::any_of(_drawingImages.begin(), _drawingImages.end(), [range](ImageIndex id) { return range.Contains(id); }))
            return false;
        auto it = std::find_if(
            _allocatedImages.begin(), _allocatedImages.end(),
            [&plugin, range](const AllocatedImageList& item) { return item.Owner == plugin && item.Range == range; });
        if (it == _allocatedImages.end())
        {
            return false;
        }

        FreeImages(it->Range);
        _allocatedImages.erase(it);
        return true;
    }

    bool DoesPluginOwnImage(const std::shared_ptr<Plugin>& plugin, ImageIndex index)
    {
        auto it = std::find_if(
            _allocatedImages.begin(), _allocatedImages.end(),
            [&plugin, index](const AllocatedImageList& item) { return item.Owner == plugin && item.Range.Contains(index); });
        return it != _allocatedImages.end();
    }

    static void FreeCustomImages(const std::shared_ptr<Plugin>& plugin)
    {
        auto it = _allocatedImages.begin();
        while (it != _allocatedImages.end())
        {
            if (it->Owner == plugin)
            {
                FreeImages(it->Range);
                it = _allocatedImages.erase(it);
            }
            else
            {
                it++;
            }
        }
    }

    void InitialiseCustomImages(ScriptEngine& scriptEngine)
    {
        scriptEngine.SubscribeToPluginStoppedEvent([](std::shared_ptr<Plugin> plugin) -> void { FreeCustomImages(plugin); });
    }

    JSValue JSGetImageInfo(JSContext* ctx, ImageIndex id)
    {
        auto* g1 = GfxGetG1Element(id);
        if (g1 == nullptr)
        {
            return JS_UNDEFINED;
        }

        JSValue obj = JS_NewObject(ctx);
        JS_SetPropertyStr(ctx, obj, "id", JS_NewInt32(ctx, id));
        JS_SetPropertyStr(ctx, obj, "offset", ToJSValue(ctx, ScreenCoordsXY{ g1->xOffset, g1->yOffset }));
        JS_SetPropertyStr(ctx, obj, "width", JS_NewInt32(ctx, g1->width));
        JS_SetPropertyStr(ctx, obj, "height", JS_NewInt32(ctx, g1->height));

        JS_SetPropertyStr(ctx, obj, "hasTransparent", JS_NewBool(ctx, g1->flags.has(G1Flag::hasTransparency)));
        JS_SetPropertyStr(ctx, obj, "isRLE", JS_NewBool(ctx, g1->flags.has(G1Flag::hasRLECompression)));
        JS_SetPropertyStr(ctx, obj, "isPalette", JS_NewBool(ctx, g1->flags.has(G1Flag::isPalette)));
        JS_SetPropertyStr(ctx, obj, "noZoom", JS_NewBool(ctx, g1->flags.has(G1Flag::noZoomDraw)));

        if (g1->flags.has(G1Flag::hasZoomSprite))
        {
            JS_SetPropertyStr(ctx, obj, "nextZoomId", JS_NewInt32(ctx, id - g1->zoomedOffset));
        }
        else
        {
            JS_SetPropertyStr(ctx, obj, "nextZoomId", JS_UNDEFINED);
        }
        return obj;
    }

    static std::string_view GetPixelDataTypeForG1(const G1Element& g1)
    {
        if (g1.flags.has(G1Flag::hasRLECompression))
            return "rle";
        else if (g1.flags.has(G1Flag::isPalette))
            return "palette";
        return "raw";
    }

    JSValue JSGetImagePixelData(JSContext* ctx, ImageIndex id)
    {
        PrepareCustomImageSource(id);
        auto* g1 = GfxGetG1Element(id);
        if (g1 == nullptr)
        {
            return JS_UNDEFINED;
        }
        auto dataSize = G1CalculateDataSize(g1);
        auto type = GetPixelDataTypeForG1(*g1);

        // Copy the G1 data to a JS buffer wrapped in a Uint8Array
        JSValue data = JS_NewUint8ArrayCopy(ctx, g1->offset, dataSize);

        JSValue obj = JS_NewObject(ctx);
        JS_SetPropertyStr(ctx, obj, "type", JSFromStdString(ctx, type));
        JS_SetPropertyStr(ctx, obj, "width", JS_NewInt32(ctx, g1->width));
        JS_SetPropertyStr(ctx, obj, "height", JS_NewInt32(ctx, g1->height));
        JS_SetPropertyStr(ctx, obj, "data", data);
        return obj;
    }

    static std::vector<uint8_t> GetDataFromBufferLikeObject(JSContext* ctx, JSValue data)
    {
        std::vector<uint8_t> result;
        if (JS_IsArray(data))
        {
            // From array of numbers
            int64_t arrSz = 0;
            JS_GetLength(ctx, data, &arrSz);
            if (arrSz > 0)
            {
                result.reserve(arrSz);
                JSIterateArray(ctx, data, [&result](JSContext* ctx2, JSValue val) { result.push_back(JSToInt(ctx2, val)); });
            }
        }
        else if (JS_IsString(data))
        {
            std::string str = JSToStdString(ctx, data);
            result = base64::decode_into<std::vector<uint8_t>>(str);
        }
        else if (JS_GetTypedArrayType(data) == JSTypedArrayEnum::JS_TYPED_ARRAY_UINT8)
        {
            // From Uint8Array
            size_t sz = 0;
            uint8_t* arr = JS_GetUint8Array(ctx, &sz, data);
            if (arr)
            {
                result = std::vector<uint8_t>(arr, arr + sz);
            }
        }
        return result;
    }

    static std::vector<uint8_t> RemovePadding(const std::vector<uint8_t>& srcData, const PixelData& pixelData)
    {
        std::vector<uint8_t> unpadded(pixelData.Width * pixelData.Height, 0);
        auto* src = srcData.data();
        auto* dst = unpadded.data();
        for (int32_t y = 0; y < pixelData.Height; y++)
        {
            std::memcpy(dst, src, pixelData.Width);
            src += pixelData.Stride;
            dst += pixelData.Width;
        }
        return unpadded;
    }

    static ImportMode getImportModeFromPalette(const PixelDataPaletteKind& palette)
    {
        switch (palette)
        {
            case PixelDataPaletteKind::closest:
                return ImportMode::closest;
            case PixelDataPaletteKind::dither:
                return ImportMode::dithering;
            case PixelDataPaletteKind::none:
            case PixelDataPaletteKind::keep:
            default:
                return ImportMode::standard;
        }
    }

    static std::vector<uint8_t> GetBufferFromPixelData(JSContext* ctx, PixelData& pixelData)
    {
        std::vector<uint8_t> imageData;
        switch (pixelData.Type)
        {
            case PixelDataKind::raw:
            {
                auto data = GetDataFromBufferLikeObject(ctx, pixelData.Data);
                if (pixelData.Stride != pixelData.Width)
                {
                    // Make sure data is expected size for RemovePadding
                    data.resize(pixelData.Stride * pixelData.Height);
                    data = RemovePadding(data, pixelData);
                }

                // Make sure data is expected size
                data.resize(pixelData.Width * pixelData.Height);
                imageData = std::move(data);
                break;
            }
            case PixelDataKind::rle:
            {
                imageData = GetDataFromBufferLikeObject(ctx, pixelData.Data);
                break;
            }
            case PixelDataKind::png:
            {
                auto imageFormat = pixelData.Palette == PixelDataPaletteKind::keep ? ImageFormat::png : ImageFormat::png32;
                auto palette = pixelData.Palette == PixelDataPaletteKind::keep ? Palette::keepIndices : Palette::openRCT2;
                auto importMode = getImportModeFromPalette(pixelData.Palette);
                auto pngData = GetDataFromBufferLikeObject(ctx, pixelData.Data);
                auto image = Imaging::ReadFromBuffer(pngData, imageFormat);
                constexpr ImportFlags flags = { ImportFlag::rle };
                ImageImportMeta meta = { { 0, 0 }, palette, flags, importMode };

                ImageImporter importer;
                auto importResult = importer.Import(image, meta);

                pixelData.Type = PixelDataKind::rle;
                pixelData.Width = importResult.Element.width;
                pixelData.Height = importResult.Element.height;

                imageData = std::move(importResult.Buffer);
                break;
            }
            default:
                throw std::runtime_error("Unsupported pixel data type.");
        }
        return imageData;
    }

    static PixelDataKind PixelDataKindFromJS(const std::string& s)
    {
        if (s == "raw")
            return PixelDataKind::raw;
        if (s == "rle")
            return PixelDataKind::rle;
        if (s == "palette")
            return PixelDataKind::palette;
        if (s == "png")
            return PixelDataKind::png;
        return PixelDataKind::unknown;
    }

    static PixelDataPaletteKind PixelDataPaletteKindFromJS(const std::string& s)
    {
        if (s == "keep")
            return PixelDataPaletteKind::keep;
        if (s == "closest")
            return PixelDataPaletteKind::closest;
        if (s == "dither")
            return PixelDataPaletteKind::dither;
        return PixelDataPaletteKind::none;
    }

    static PixelData GetPixelDataFromJS(JSContext* ctx, JSValue jsPixelData)
    {
        PixelData pixelData;
        pixelData.Type = PixelDataKindFromJS(JSToStdString(ctx, jsPixelData, "type"));
        pixelData.Palette = PixelDataPaletteKindFromJS(JSToStdString(ctx, jsPixelData, "palette"));
        pixelData.Width = AsOrDefault(ctx, jsPixelData, "width", static_cast<int32_t>(0));
        pixelData.Height = AsOrDefault(ctx, jsPixelData, "height", static_cast<int32_t>(0));
        pixelData.Stride = AsOrDefault(ctx, jsPixelData, "stride", static_cast<int32_t>(pixelData.Width));
        // Note: this must be JS_FreeValued
        pixelData.Data = JS_GetPropertyStr(ctx, jsPixelData, "data");
        return pixelData;
    }

    static void ReplacePixelDataForImage(ImageIndex id, const PixelData& pixelData, std::vector<uint8_t>&& data)
    {
        // Allocate before releasing the previous image so a caught allocation failure leaves it usable.
        auto newData = new uint8_t[data.size()];
        std::memcpy(newData, data.data(), data.size());

        // Setup the g1 element
        G1Element el{};
        auto* lastel = GfxGetG1Element(id);
        if (lastel != nullptr)
        {
            el = *lastel;
            delete[] el.offset;
        }

        el.offset = newData;
        el.width = pixelData.Width;
        el.height = pixelData.Height;
        el.flags = {};
        if (pixelData.Type == PixelDataKind::rle)
        {
            el.flags.set(G1Flag::hasRLECompression);
        }
        GfxSetG1Element(id, &el);
        DrawingEngineInvalidateImage(id);
    }

    void JSSetPixelData(JSContext* ctx, ImageIndex id, JSValue jsPixelData)
    {
        if (IsDrawingImage(id))
            throw std::runtime_error("Cannot replace an image during its drawing callback");
        FlushCustomImageDrawing();
        auto pixelData = GetPixelDataFromJS(ctx, jsPixelData);
        try
        {
            auto newData = GetBufferFromPixelData(ctx, pixelData);
            ReplacePixelDataForImage(id, pixelData, std::move(newData));
        }
        catch (...)
        {
            JS_FreeValue(ctx, pixelData.Data);
            throw;
        }
        JS_FreeValue(ctx, pixelData.Data);
    }

    // A callback can enter another draw or read its in-place image. Only one recorder owns the shared service at a time.
    // Switching recorders materialises indices; resuming imports them without rerunning any script callback.
    class CustomImageDrawing;
    static CustomImageDrawing* _activeDrawing{};

    class CustomImageDrawing
    {
        std::shared_ptr<Plugin> _plugin;
        ImageIndex _id;
        G1Element _image{};
        bool _inPlace{};
        OffscreenRenderRequest _request;
        std::unique_ptr<IRenderSession> _session;
        RenderTarget* _boundTarget{};
        ptrdiff_t _clipOffset{};
        std::exception_ptr _failure;

        void RefreshInPlaceSeed()
        {
            if (!_inPlace)
                return;
            if (!DoesPluginOwnImage(_plugin, _id))
                throw std::runtime_error("Custom image owner stopped during its drawing callback");
            const auto* current = GfxGetG1Element(_id);
            if (current == nullptr || current->width != static_cast<int32_t>(_request.logicalExtent.width)
                || current->height != static_cast<int32_t>(_request.logicalExtent.height)
                || current->flags.has(G1Flag::hasRLECompression) || current->offset == nullptr)
                throw std::runtime_error("Custom image changed during its drawing callback");
            _image = *current;
            std::memcpy(_request.initialIndices.data(), current->offset, _request.initialIndices.size());
        }

        void Publish(bool final)
        {
            if (!DoesPluginOwnImage(_plugin, _id))
                throw std::runtime_error("Custom image owner stopped during its drawing callback");
            const auto& bytes = _request.initialIndices;
            auto pixels = std::make_unique<uint8_t[]>(bytes.size());
            std::memcpy(pixels.get(), bytes.data(), bytes.size());
            // Allocations in callbacks can relocate sprite storage. Reacquire rather than retaining a G1 pointer.
            const auto* previous = GfxGetG1Element(_id);
            auto* previousPixels = previous == nullptr ? nullptr : previous->offset;
            auto image = _image;
            image.offset = pixels.get();
            if (final)
            {
                image.width = static_cast<int16_t>(_request.logicalExtent.width);
                image.height = static_cast<int16_t>(_request.logicalExtent.height);
                if (!_inPlace)
                    image.flags = {};
                image.flags.set(G1Flag::hasTransparency);
            }
            GfxSetG1Element(_id, &image);
            pixels.release();
            delete[] previousPixels;
            DrawingEngineInvalidateImage(_id);
        }

        void Begin()
        {
            if (_failure)
                std::rethrow_exception(_failure);
            if (_activeDrawing == this)
                return;
            try
            {
                FlushCustomImageDrawing();
                // Nested same-image callbacks publish into the original in-place destination.
                RefreshInPlaceSeed();
                _session = GetContext()->GetRenderService().BeginOffscreen(_request);
                if (!_session)
                    throw std::runtime_error("Custom image has no render session");
                auto& rt = _session->GetRenderTarget();
                if (rt.x != 0 || rt.y != 0 || rt.width != static_cast<int32_t>(_request.logicalExtent.width)
                    || rt.height != static_cast<int32_t>(_request.logicalExtent.height) || rt.pitch != 0
                    || rt.bits == nullptr || rt.DrawingEngine == nullptr || rt.zoom_level != ZoomLevel{})
                    throw std::runtime_error("Custom image render target differs");
                _activeDrawing = this;
            }
            catch (...)
            {
                _session.reset();
                _failure = std::current_exception();
                throw;
            }
        }

    public:
        CustomImageDrawing(std::shared_ptr<Plugin> plugin, ImageIndex id, ScreenSize size)
            : _plugin(std::move(plugin)), _id(id)
        {
            FlushCustomImageDrawing();
            const auto* original = GfxGetG1Element(id);
            _image = original == nullptr ? G1Element{} : *original;
            _inPlace = original != nullptr && original->width == size.width && original->height == size.height
                && !original->flags.has(G1Flag::hasRLECompression);
            _request.name = "script-custom-image";
            _request.logicalExtent = { static_cast<uint32_t>(size.width), static_cast<uint32_t>(size.height) };
            _request.outputExtent = _request.logicalExtent;
            _request.initialContents = RenderInitialContents::ownedIndices;
            _request.initialIndices.resize(static_cast<size_t>(size.width) * size.height);
            if (_inPlace && original->offset != nullptr)
                std::memcpy(_request.initialIndices.data(), original->offset, _request.initialIndices.size());
            for (size_t i = 0; i < _request.palette.size(); ++i)
            {
                const auto colour = gPalette[i];
                _request.palette[i] = { colour.red, colour.green, colour.blue, colour.alpha };
            }
            Begin();
            try
            {
                if (!_inPlace && original != nullptr)
                    GfxDrawSprite(_session->GetRenderTarget(), ImageId(id), { 0, 0 });
            }
            catch (...)
            {
                _activeDrawing = nullptr;
                throw;
            }
        }
        ~CustomImageDrawing()
        {
            if (_activeDrawing == this)
                _activeDrawing = nullptr;
        }
        RenderTarget& Target() { return _session->GetRenderTarget(); }
        void RecordingFailed(std::exception_ptr error) noexcept
        {
            if (!_failure)
                _failure = std::move(error);
            if (_activeDrawing == this)
                _activeDrawing = nullptr;
            if (_session)
                _session->Cancel();
            _session.reset();
            if (_boundTarget != nullptr)
            {
                _boundTarget->bits = nullptr;
                _boundTarget->DrawingEngine = nullptr;
            }
        }
        void Bind(RenderTarget& target)
        {
            Begin();
            const auto& full = _session->GetRenderTarget();
            if (target.width <= 0 || target.height <= 0)
                _clipOffset = 0;
            // Preserve the callback's clip/local coordinates when the recording target is recreated.
            if (_boundTarget == nullptr || target.DrawingEngine != full.DrawingEngine || target.bits == nullptr)
            {
                target.bits = full.bits + _clipOffset;
                target.DrawingEngine = full.DrawingEngine;
            }
            _boundTarget = &target;
        }
        void Flush()
        {
            if (_failure)
                std::rethrow_exception(_failure);
            if (!_session)
                return;
            try
            {
                if (_boundTarget != nullptr && !_request.orderedAlias)
                {
                    _clipOffset = 0;
                    if (_boundTarget->width > 0 && _boundTarget->height > 0)
                    {
                        const auto base = reinterpret_cast<uintptr_t>(_session->GetRenderTarget().bits);
                        const auto clipped = reinterpret_cast<uintptr_t>(_boundTarget->bits);
                        if (_boundTarget->bits == nullptr || clipped < base
                            || clipped - base >= _request.initialIndices.size())
                            throw std::runtime_error("Custom image clip lies outside its target");
                        _clipOffset = static_cast<ptrdiff_t>(clipped - base);
                    }
                    _boundTarget->bits = nullptr;
                    _boundTarget->DrawingEngine = nullptr;
                }
                auto completion = _session->Submit();
                if (!completion)
                    throw std::runtime_error("Custom image has no render completion");
                const auto outcome = completion->Wait(std::chrono::seconds(120));
                if (outcome.error)
                    throw RenderServiceException(*outcome.error);
                if (!outcome.result)
                    throw std::runtime_error("Custom image has no owned output");
                const auto& result = *outcome.result;
                if (outcome.identity != completion->GetIdentity() || result.identity != outcome.identity
                    || result.identity.submissionId == 0 || result.identity.targetId == 0 || result.identity.targetGeneration == 0
                    || result.identity.name != _request.name || result.logicalExtent != _request.logicalExtent
                    || result.outputExtent != _request.outputExtent || result.palette != _request.palette
                    || result.indexed.size() != _request.initialIndices.size() || !result.rgba.empty())
                    throw std::runtime_error("Custom image readback contract differs");
                _request.initialIndices = result.indexed;
                _session.reset();
                _activeDrawing = nullptr;
                if (_inPlace)
                    Publish(false);
            }
            catch (...)
            {
                _session.reset();
                _activeDrawing = nullptr;
                _failure = std::current_exception();
                throw;
            }
        }
        void Finish()
        {
            FlushCustomImageDrawing();
            Flush();
            RefreshInPlaceSeed();
            Publish(true);
        }
        bool DrawOrderedAlias(RenderTarget& target, ImageId image, ScreenCoordsXY position)
        {
            if (!IsInPlaceSource(image.GetIndex()))
                return false;
            Bind(target);
            if (target.width <= 0 || target.height <= 0 || _image.flags.has(G1Flag::one))
                return true;
            if (target.zoom_level != ZoomLevel{} || image.IsBlended() || !image.HasTertiary())
                throw std::runtime_error("Unexpected custom bitmap alias drawing mode");
            // Preserve the legacy bitmap blitter's signed-16-bit placement before clipping, without signed overflow.
            const auto narrow = [](int64_t value) -> int32_t {
                const auto bits = static_cast<uint16_t>(value);
                return bits < 0x8000u ? bits : static_cast<int32_t>(bits) - 0x10000;
            };
            const int32_t left = narrow(static_cast<int64_t>(position.x) + _image.xOffset - target.WorldX());
            const int32_t top = narrow(static_cast<int64_t>(narrow(static_cast<int64_t>(position.y) + _image.yOffset))
                - target.WorldY());
            const int32_t sx = std::max(0, -left), sy = std::max(0, -top);
            const int32_t dx = std::max(0, left), dy = std::max(0, top);
            const int32_t width = std::min(static_cast<int32_t>(_image.width) - sx, target.width - dx);
            const int32_t height = std::min(static_cast<int32_t>(_image.height) - sy, target.height - dy);
            if (width <= 0 || height <= 0)
                return true;
            const auto base = reinterpret_cast<uintptr_t>(_session->GetRenderTarget().bits);
            const auto clipped = reinterpret_cast<uintptr_t>(target.bits);
            const auto stride = _request.logicalExtent.width;
            if (target.bits == nullptr || clipped < base || clipped - base >= _request.initialIndices.size()
                || target.LineStride() != static_cast<int32_t>(stride))
                throw std::runtime_error("Invalid custom bitmap alias clip");
            const auto offset = static_cast<uint32_t>(clipped - base);
            const auto destinationX = offset % stride + static_cast<uint32_t>(dx);
            const auto destinationY = offset / stride + static_cast<uint32_t>(dy);
            // Disjoint and identical-position operations have no cross-pixel recurrence.
            if ((destinationX == static_cast<uint32_t>(sx) && destinationY == static_cast<uint32_t>(sy))
                || destinationX >= static_cast<uint32_t>(sx + width) || static_cast<uint32_t>(sx) >= destinationX + width
                || destinationY >= static_cast<uint32_t>(sy + height) || static_cast<uint32_t>(sy) >= destinationY + height)
                return false;
            OrderedImageAlias alias{ .sourceX = static_cast<uint32_t>(sx), .sourceY = static_cast<uint32_t>(sy),
                .destinationX = destinationX, .destinationY = destinationY,
                .width = static_cast<uint32_t>(width), .height = static_cast<uint32_t>(height),
                .skipSourceZero = true, .skipMappedZero = true };
            // GraphicsContext.image always supplies the legacy three-colour remap, even for default colours.
            // Copy metadata now: GfxDrawSpriteGetPalette returns a borrowed thread-local table.
            const auto map = GfxDrawSpriteGetPalette(image).value_or(PaletteMap::GetDefault());
            for (size_t i = 0; i < alias.remap.size(); ++i)
                alias.remap[i] = static_cast<uint8_t>(map[i]);
            Flush();
            _request.orderedAlias = alias;
            try
            {
                _session = GetContext()->GetRenderService().BeginOffscreen(_request);
                if (!_session)
                    throw std::runtime_error("Custom bitmap alias has no render session");
                _activeDrawing = this;
                Flush();
                _request.orderedAlias.reset();
            }
            catch (...)
            {
                _request.orderedAlias.reset();
                RecordingFailed(std::current_exception());
                throw;
            }
            return true;
        }
        bool IsInPlaceSource(ImageIndex id) const { return id == _id && _inPlace; }
    };

    static void FlushCustomImageDrawing()
    {
        if (_activeDrawing != nullptr)
            _activeDrawing->Flush();
    }

    void PrepareCustomImageSource(ImageIndex id)
    {
        if (_activeDrawing != nullptr && _activeDrawing->IsInPlaceSource(id))
            _activeDrawing->Flush();
    }

    void JSDrawCustomImage(
        JSContext* ctx, ScriptEngine& scriptEngine, ImageIndex id, ScreenSize size, const JSCallback& callback)
    {
        auto plugin = scriptEngine.GetExecInfo().GetCurrentPlugin();
        if (size.width <= 0 || size.height <= 0 || size.width > std::numeric_limits<int16_t>::max()
            || size.height > std::numeric_limits<int16_t>::max())
            throw std::runtime_error("Invalid custom image dimensions");
        // Match the configured service's bounded target pool before allocating/copying an owned seed.
        // Larger historical custom images need single-record tiled execution; do not replay a script callback per tile.
        constexpr uint64_t kMaxCustomImagePixels = 4 * 1024 * 1024;
        if (static_cast<uint64_t>(size.width) * size.height > kMaxCustomImagePixels)
            throw std::runtime_error("Custom image exceeds the 4M-pixel Vulkan target limit");
        if (IsDrawingImage(id))
        {
            const auto* current = GfxGetG1Element(id);
            if (current == nullptr || current->width != size.width || current->height != size.height
                || current->flags.has(G1Flag::hasRLECompression))
                throw std::runtime_error("Cannot resize or convert an image during its drawing callback");
        }
        if (!DoesPluginOwnImage(plugin, id))
            throw std::runtime_error("This plugin did not allocate the image");
        _drawingImages.push_back(id);
        struct DrawingScope
        {
            ~DrawingScope() { _drawingImages.pop_back(); }
        } drawingScope;
        CustomImageDrawing drawing(plugin, id, size);
        if (callback.IsValid())
        {
            auto graphics = gScGraphicsContext.New(
                ctx, drawing.Target(), [&drawing](RenderTarget& rt) { drawing.Bind(rt); },
                [&drawing](std::exception_ptr error) { drawing.RecordingFailed(std::move(error)); },
                [&drawing](RenderTarget& rt, ImageId image, ScreenCoordsXY position) {
                    return drawing.DrawOrderedAlias(rt, image, position);
                });
            struct GraphicsScope
            {
                JSContext* context;
                JSValue value;
                ~GraphicsScope()
                {
                    gScGraphicsContext.Invalidate(value);
                    JS_FreeValue(context, value);
                }
            } graphicsScope{ ctx, graphics };
            // Log callback exceptions and retain preceding drawing, as ExecutePluginCall has always done.
            scriptEngine.ExecutePluginCall(plugin, callback.callback, { graphics }, false, true);
            // Finish before the graphics object can be freed: Flush retains its target only within this scope.
            drawing.Finish();
        }
        else
            drawing.Finish();
    }
} // namespace OpenRCT2::Scripting

#endif
