/*****************************************************************************
 * Copyright (c) 2014-2026 OpenRCT2 developers
 *
 * For a complete list of all authors, please refer to contributors.md
 * Interested in contributing? Visit https://github.com/OpenRCT2/OpenRCT2
 *
 * OpenRCT2 is licensed under the GNU General Public License version 3.
 *****************************************************************************/

#pragma once

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
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

    // One atlas-slot lookup retained by the backend. Sprite commands reference
    // this compact table instead of repeating atlas origin and layer per draw.
    struct SpriteAssetDescriptor
    {
        Int2 atlasOrigin;
        int32_t atlasLayer;
        int32_t reserved;
    };

    struct SpriteCommand
    {
        Int4 clip;
        Int4 bounds;
        Int2 texelOffset;
        uint32_t asset;
        uint32_t palettes;
        uint32_t effects;
        int32_t depth;
        float zoom;

        [[nodiscard]] static constexpr uint32_t PackPalettes(
            uint8_t primary, uint8_t secondary, uint8_t tertiary, uint8_t count) noexcept
        {
            return static_cast<uint32_t>(primary) | (static_cast<uint32_t>(secondary) << 8)
                | (static_cast<uint32_t>(tertiary) << 16) | (static_cast<uint32_t>(count) << 24);
        }

        [[nodiscard]] static constexpr uint32_t PackEffects(uint32_t flags, uint8_t colour) noexcept
        {
            return (flags & 0xffffu) | (static_cast<uint32_t>(colour) << 16);
        }

        [[nodiscard]] static constexpr uint8_t GetPalette(uint32_t packed, uint32_t index) noexcept
        {
            return static_cast<uint8_t>((packed >> (index * 8)) & 0xffu);
        }

        [[nodiscard]] static constexpr uint8_t GetPaletteCount(uint32_t packed) noexcept
        {
            return static_cast<uint8_t>(packed >> 24);
        }

        [[nodiscard]] static constexpr uint32_t GetEffectFlags(uint32_t packed) noexcept
        {
            return packed & 0xffffu;
        }

        [[nodiscard]] static constexpr uint8_t GetEffectColour(uint32_t packed) noexcept
        {
            return static_cast<uint8_t>((packed >> 16) & 0xffu);
        }
    };

#pragma pack(pop)

    // Unlike native draw commands, first-use atlas pixels must survive the
    // recorder/backend handoff. Keeping them in the frame stream lets a future
    // render thread allocate its own staging slice without retaining caller or
    // Vulkan upload-ring memory.
    struct TextureUpload
    {
        uint32_t atlas = 0;
        Int4 bounds{};
        uint32_t sourcePitch = 0;
        uint32_t descriptorIndex = 0;
        SpriteAssetDescriptor descriptor{};
        std::vector<std::byte> pixels;
    };

    /** Owned, viewport-resolved LightFX data for one visual frame. */
    struct LightFxCommand
    {
        int32_t destinationX = 0;
        int32_t destinationY = 0;
        uint32_t width = 0;
        uint32_t height = 0;
        uint32_t sourceOffset = 0;
        uint32_t sourceStride = 0;
        uint32_t type = 0;
        uint32_t intensity = 0;
    };

    static_assert(std::is_trivially_copyable_v<LightFxCommand>);
    static_assert(sizeof(LightFxCommand) == 32);
    static_assert(offsetof(LightFxCommand, destinationX) == 0);
    static_assert(offsetof(LightFxCommand, destinationY) == 4);
    static_assert(offsetof(LightFxCommand, width) == 8);
    static_assert(offsetof(LightFxCommand, height) == 12);
    static_assert(offsetof(LightFxCommand, sourceOffset) == 16);
    static_assert(offsetof(LightFxCommand, sourceStride) == 20);
    static_assert(offsetof(LightFxCommand, type) == 24);
    static_assert(offsetof(LightFxCommand, intensity) == 28);

    template<typename ResolvedLight>
    [[nodiscard]] constexpr LightFxCommand MakeLightFxCommand(const ResolvedLight& light) noexcept
    {
        return {
            .destinationX = light.destinationX,
            .destinationY = light.destinationY,
            .width = light.width,
            .height = light.height,
            .sourceOffset = light.sourceOffset,
            .sourceStride = light.sourceStride,
            .type = light.type,
            .intensity = light.intensity,
        };
    }

    // Keep the local sizes synchronized with lightfx_accumulate.comp.
    constexpr uint32_t kLightFxComputeLocalSizeX = 16;
    constexpr uint32_t kLightFxComputeLocalSizeY = 16;
    constexpr uint32_t kMaximumLightFxCommandCount = 15999;

    [[nodiscard]] constexpr bool AreLightFxComputeLimitsSufficient(
        uint32_t maxInvocations, uint32_t maxSizeX, uint32_t maxSizeY, uint32_t maxGroupCountZ) noexcept
    {
        return maxInvocations >= kLightFxComputeLocalSizeX * kLightFxComputeLocalSizeY
            && maxSizeX >= kLightFxComputeLocalSizeX && maxSizeY >= kLightFxComputeLocalSizeY
            && maxGroupCountZ >= kMaximumLightFxCommandCount;
    }

    [[nodiscard]] constexpr uint32_t GetLightFxTextureSize(uint32_t type) noexcept
    {
        if (type < 4 || type > 11)
        {
            return 0;
        }
        return 32u << ((type - 4u) & 3u);
    }

    [[nodiscard]] constexpr uint32_t GetLightFxContribution(uint32_t falloff, uint32_t intensity) noexcept
    {
        return intensity == 255 ? falloff : (falloff * (1 + intensity)) >> 8;
    }

    [[nodiscard]] constexpr bool IsValidLightFxCommand(
        const LightFxCommand& command, uint32_t canvasWidth, uint32_t canvasHeight) noexcept
    {
        const uint32_t textureSize = GetLightFxTextureSize(command.type);
        if (textureSize == 0 || command.intensity > 255 || command.destinationX < 0 || command.destinationY < 0
            || command.width == 0 || command.height == 0 || command.sourceStride < command.width)
        {
            return false;
        }
        const auto destinationX = static_cast<uint32_t>(command.destinationX);
        const auto destinationY = static_cast<uint32_t>(command.destinationY);
        if (destinationX > canvasWidth || command.width > canvasWidth - destinationX || destinationY > canvasHeight
            || command.height > canvasHeight - destinationY)
        {
            return false;
        }
        const uint64_t finalSource = static_cast<uint64_t>(command.sourceOffset)
            + static_cast<uint64_t>(command.height - 1) * command.sourceStride + command.width;
        return finalSource <= static_cast<uint64_t>(textureSize) * textureSize;
    }

    struct LightFxFrameSnapshot
    {
        uint32_t width = 0;
        uint32_t height = 0;
        std::array<std::byte, 256 * 4> lightPalette{};
        std::vector<std::byte> intensities;
        std::vector<LightFxCommand> lights;

        [[nodiscard]] bool IsValid() const noexcept
        {
            if (width == 0 || height == 0 || width > std::numeric_limits<size_t>::max() / height
                || (!intensities.empty() && intensities.size() != static_cast<size_t>(width) * height))
            {
                return false;
            }
            return std::all_of(lights.begin(), lights.end(), [this](const auto& light) {
                return IsValidLightFxCommand(light, width, height);
            });
        }

        [[nodiscard]] bool HasCpuIntensity() const noexcept
        {
            return !intensities.empty();
        }
    };

    [[nodiscard]] constexpr bool InclusiveRectIntersectsClip(const Int4& bounds, const Int4& clip) noexcept
    {
        return bounds.x <= bounds.z && bounds.y <= bounds.w && bounds.z >= clip.x && bounds.w >= clip.y
            && bounds.x < clip.z && bounds.y < clip.w;
    }

    static_assert(std::is_trivially_copyable_v<LineCommand>);
    static_assert(std::is_trivially_copyable_v<RectCommand>);
    static_assert(std::is_trivially_copyable_v<SpriteAssetDescriptor>);
    static_assert(std::is_trivially_copyable_v<SpriteCommand>);
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
    static_assert(sizeof(SpriteAssetDescriptor) == 16);
    static_assert(offsetof(SpriteAssetDescriptor, atlasOrigin) == 0);
    static_assert(offsetof(SpriteAssetDescriptor, atlasLayer) == 8);
    static_assert(sizeof(SpriteCommand) == 60);
    static_assert(offsetof(SpriteCommand, clip) == 0);
    static_assert(offsetof(SpriteCommand, bounds) == 16);
    static_assert(offsetof(SpriteCommand, texelOffset) == 32);
    static_assert(offsetof(SpriteCommand, asset) == 40);
    static_assert(offsetof(SpriteCommand, palettes) == 44);
    static_assert(offsetof(SpriteCommand, effects) == 48);
    static_assert(offsetof(SpriteCommand, depth) == 52);
    static_assert(offsetof(SpriteCommand, zoom) == 56);
    static_assert(sizeof(WeatherCommand) == 28);
    static_assert(offsetof(WeatherCommand, bounds) == 0);
    static_assert(offsetof(WeatherCommand, offset) == 16);
    static_assert(offsetof(WeatherCommand, pattern) == 24);

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
        CommandBatch<SpriteCommand> opaqueSprites;
        CommandBatch<RectCommand> transparentRects;
        CommandBatch<WeatherCommand> weather;
        std::vector<TextureUpload> textureUploads;
        std::optional<LightFxFrameSnapshot> lightFx;

        void clear() noexcept // NOLINT(readability-identifier-naming)
        {
            lines.clear();
            opaqueRects.clear();
            opaqueSprites.clear();
            transparentRects.clear();
            weather.clear();
            textureUploads.clear();
            if (lightFx.has_value())
            {
                lightFx->width = 0;
                lightFx->height = 0;
                lightFx->intensities.clear();
                lightFx->lights.clear();
            }
        }

        void reserveForParkView()
        {
            lines.reserve(4096);
            opaqueRects.reserve(8192);
            opaqueSprites.reserve(32768);
            transparentRects.reserve(4096);
            weather.reserve(64);
            textureUploads.reserve(512);
        }
    };
} // namespace OpenRCT2::Ui::Gpu
