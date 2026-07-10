/*****************************************************************************
 * Copyright (c) 2014-2026 OpenRCT2 developers
 *
 * For a complete list of all authors, please refer to contributors.md
 * Interested in contributing? Visit https://github.com/OpenRCT2/OpenRCT2
 *
 * OpenRCT2 is licensed under the GNU General Public License version 3.
 *****************************************************************************/

#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <type_traits>
#include <vector>

namespace OpenRCT2::Ui::Gpu
{
#pragma pack(push, 1)
    struct Int2
    {
        int32_t x;
        int32_t y;

        template<typename T>
        constexpr Int2& operator=(const T& other) noexcept
        {
            x = static_cast<int32_t>(other.x);
            y = static_cast<int32_t>(other.y);
            return *this;
        }
    };

    struct Int3
    {
        int32_t x;
        int32_t y;
        int32_t z;

        template<typename T>
        constexpr Int3& operator=(const T& other) noexcept
        {
            x = static_cast<int32_t>(other.x);
            y = static_cast<int32_t>(other.y);
            z = static_cast<int32_t>(other.z);
            return *this;
        }
    };

    struct Int4
    {
        int32_t x;
        int32_t y;
        int32_t z;
        int32_t w;

        template<typename T>
        constexpr Int4& operator=(const T& other) noexcept
        {
            x = static_cast<int32_t>(other.x);
            y = static_cast<int32_t>(other.y);
            z = static_cast<int32_t>(other.z);
            w = static_cast<int32_t>(other.w);
            return *this;
        }
    };

    struct Float4
    {
        float x;
        float y;
        float z;
        float w;

        template<typename T>
        constexpr Float4& operator=(const T& other) noexcept
        {
            x = static_cast<float>(other.x);
            y = static_cast<float>(other.y);
            z = static_cast<float>(other.z);
            w = static_cast<float>(other.w);
            return *this;
        }
    };

    struct LineCommand
    {
        Int4 bounds;
        uint32_t colour;
        int32_t depth;
    };

    // One weather pattern pass over a visible viewport region. Backends
    // expand this into the legacy sparse 32x32 precipitation pattern.
    struct WeatherCommand
    {
        Int4 bounds;
        Int2 offset;
        int32_t pattern;
    };

    struct RectCommand
    {
        Int4 clip;
        int32_t texColourAtlas;
        Float4 texColourBounds;
        int32_t texMaskAtlas;
        Float4 texMaskBounds;
        Int3 palettes;
        int32_t flags;
        uint32_t colour;
        Int4 bounds;
        int32_t depth;
        float zoom;

        enum : uint32_t
        {
            FLAG_NO_TEXTURE = (1u << 2u),
            FLAG_MASK = (1u << 3u),
            FLAG_CROSS_HATCH = (1u << 4u),
            FLAG_TTF_TEXT = (1u << 5u),
            // Bits 8 to 16 store the TTF hinting threshold.
            FLAG_TTF_HINTING_THRESHOLD_MASK = 0xff00,
        };
    };

    struct TextureUpload
    {
        uint32_t atlas;
        Int4 bounds;
        uint32_t sourceOffset;
        uint32_t sourcePitch;
    };

    struct CanvasUpload
    {
        uint32_t sourceOffset;
        uint32_t sourcePitch;
        uint32_t width;
        uint32_t height;
    };
#pragma pack(pop)

    [[nodiscard]] constexpr bool InclusiveRectIntersectsClip(const Int4& bounds, const Int4& clip) noexcept
    {
        return bounds.x <= bounds.z && bounds.y <= bounds.w && bounds.z >= clip.x && bounds.w >= clip.y
            && bounds.x < clip.z && bounds.y < clip.w;
    }

    static_assert(std::is_trivially_copyable_v<LineCommand>);
    static_assert(std::is_trivially_copyable_v<RectCommand>);
    static_assert(std::is_trivially_copyable_v<WeatherCommand>);
    static_assert(sizeof(LineCommand) == 24);
    static_assert(offsetof(LineCommand, bounds) == 0);
    static_assert(offsetof(LineCommand, colour) == 16);
    static_assert(offsetof(LineCommand, depth) == 20);
    static_assert(sizeof(RectCommand) == 100);
    static_assert(offsetof(RectCommand, clip) == 0);
    static_assert(offsetof(RectCommand, texColourAtlas) == 16);
    static_assert(offsetof(RectCommand, texColourBounds) == 20);
    static_assert(offsetof(RectCommand, texMaskAtlas) == 36);
    static_assert(offsetof(RectCommand, texMaskBounds) == 40);
    static_assert(offsetof(RectCommand, palettes) == 56);
    static_assert(offsetof(RectCommand, flags) == 68);
    static_assert(offsetof(RectCommand, colour) == 72);
    static_assert(offsetof(RectCommand, bounds) == 76);
    static_assert(offsetof(RectCommand, depth) == 92);
    static_assert(offsetof(RectCommand, zoom) == 96);
    static_assert(sizeof(WeatherCommand) == 28);
    static_assert(offsetof(WeatherCommand, bounds) == 0);
    static_assert(offsetof(WeatherCommand, offset) == 16);
    static_assert(offsetof(WeatherCommand, pattern) == 24);
    static_assert(sizeof(CanvasUpload) == 16);

    template<typename T>
    class CommandBatch
    {
        static_assert(std::is_trivially_copyable_v<T>);

    private:
        std::vector<T> _storage;
        size_t _size = 0;

    public:
        [[nodiscard]] bool empty() const noexcept // NOLINT(readability-identifier-naming)
        {
            return _size == 0;
        }

        [[nodiscard]] size_t size() const noexcept // NOLINT(readability-identifier-naming)
        {
            return _size;
        }

        [[nodiscard]] size_t capacity() const noexcept // NOLINT(readability-identifier-naming)
        {
            return _storage.size();
        }

        void clear() noexcept // NOLINT(readability-identifier-naming)
        {
            _size = 0;
        }

        void reserve(size_t count) // NOLINT(readability-identifier-naming)
        {
            if (count > _storage.size())
            {
                _storage.resize(count);
            }
        }

        T& allocate() // NOLINT(readability-identifier-naming)
        {
            if (_size == _storage.size())
            {
                const size_t nextCapacity = _storage.empty() ? 256 : _storage.size() * 2;
                _storage.resize(nextCapacity);
            }
            return _storage[_size++];
        }

        T& insert(const T& value) // NOLINT(readability-identifier-naming)
        {
            auto& result = allocate();
            result = value;
            return result;
        }

        [[nodiscard]] const T* data() const noexcept // NOLINT(readability-identifier-naming)
        {
            return _storage.data();
        }

        const T& operator[](size_t index) const
        {
            return _storage.at(index);
        }

        auto begin() noexcept // NOLINT(readability-identifier-naming)
        {
            return _storage.begin();
        }

        auto begin() const noexcept // NOLINT(readability-identifier-naming)
        {
            return _storage.cbegin();
        }

        auto end() noexcept // NOLINT(readability-identifier-naming)
        {
            return _storage.begin() + _size;
        }

        auto end() const noexcept // NOLINT(readability-identifier-naming)
        {
            return _storage.cbegin() + _size;
        }
    };

    struct FrameCommandStream
    {
        CommandBatch<LineCommand> lines;
        CommandBatch<RectCommand> opaqueRects;
        CommandBatch<RectCommand> transparentRects;
        CommandBatch<WeatherCommand> weather;
        CommandBatch<TextureUpload> textureUploads;
        std::optional<CanvasUpload> canvasUpload;

        void clear() noexcept // NOLINT(readability-identifier-naming)
        {
            lines.clear();
            opaqueRects.clear();
            transparentRects.clear();
            weather.clear();
            textureUploads.clear();
            canvasUpload.reset();
        }

        void reserveForParkView()
        {
            lines.reserve(4096);
            opaqueRects.reserve(32768);
            transparentRects.reserve(4096);
            weather.reserve(64);
            textureUploads.reserve(512);
        }
    };
} // namespace OpenRCT2::Ui::Gpu
