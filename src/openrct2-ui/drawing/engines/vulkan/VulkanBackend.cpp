/*****************************************************************************
 * Copyright (c) 2014-2026 OpenRCT2 developers
 *
 * For a complete list of all authors, please refer to contributors.md
 * Interested in contributing? Visit https://github.com/OpenRCT2/OpenRCT2
 *
 * OpenRCT2 is licensed under the GNU General Public License version 3.
 *****************************************************************************/

#ifdef ENABLE_VULKAN

    #include "VulkanBackend.h"

    #include <SDL.h>
    #include <algorithm>
    #include <chrono>
    #include <cstring>
    #include <limits>
    #include <stdexcept>

namespace OpenRCT2::Ui::Vulkan
{
    namespace
    {
        using Clock = std::chrono::steady_clock;

        double ToMicroseconds(Clock::duration duration)
        {
            return std::chrono::duration<double, std::micro>(duration).count();
        }

        bool HasUnsupportedDrawCommands(const Gpu::FrameCommandStream& commands)
        {
            return !commands.opaqueRects.empty() || !commands.transparentRects.empty() || !commands.weather.empty();
        }
    } // namespace

    Backend::~Backend()
    {
        Dispose();
    }

    void Backend::Initialise(const Gpu::BackendConfig& config)
    {
        Dispose();
        if (config.nativeWindow == nullptr)
        {
            throw std::invalid_argument("Vulkan backend requires an SDL window");
        }
        if (config.shaderDirectory.empty())
        {
            throw std::invalid_argument("Vulkan backend requires the installed SPIR-V shader directory");
        }

        _config = config;
        auto* window = static_cast<SDL_Window*>(config.nativeWindow);
        _device.Initialise(
            window, config.presentMode == Gpu::PresentMode::VSync,
            static_cast<VkDeviceSize>(config.uploadRingBytesPerFrame));
        _resources.Initialise(_device, config.logicalExtent);
        _linePipeline.Initialise(_device, _resources, config.shaderDirectory);
        _palettePipeline.Initialise(_device, _resources, config.shaderDirectory);

        _pendingPalette.fill(std::byte{ 0 });
        for (size_t i = 0; i < 256; i++)
        {
            _pendingPalette[i * 4 + 3] = i == 0 ? std::byte{ 0 } : std::byte{ 0xff };
        }
        _paletteDirty = true;
        PopulateCapabilities();
    }

    void Backend::Dispose()
    {
        if (_device.GetDevice() != VK_NULL_HANDLE)
        {
            // Destruction must not throw. Waiting here keeps dependent image
            // and pipeline destruction ahead of logical-device destruction.
            (void)vkDeviceWaitIdle(_device.GetDevice());
        }
        _palettePipeline.Dispose();
        _linePipeline.Dispose();
        _resources.Dispose();
        _device.Dispose();
        _config = {};
        _capabilities = {};
        _activeToken.reset();
        _activeFrame.reset();
        _latestTimings.reset();
        _paletteDirty = true;
        _submitted = false;
        _invalidatedImages.clear();
    }

    const Gpu::BackendCapabilities& Backend::GetCapabilities() const noexcept
    {
        return _capabilities;
    }

    void Backend::Resize(Gpu::Extent logicalExtent)
    {
        if (_activeToken.has_value())
        {
            throw std::logic_error("Cannot resize the Vulkan backend during a frame");
        }
        _config.logicalExtent = logicalExtent;
        _linePipeline.Dispose();
        _resources.Resize(logicalExtent);
        _linePipeline.Initialise(_device, _resources, _config.shaderDirectory);
        _palettePipeline.RefreshDescriptors(_resources);
        _device.RequestSwapchainRecreate();
    }

    void Backend::SetPresentMode(Gpu::PresentMode mode)
    {
        _config.presentMode = mode;
        _device.SetVSync(mode == Gpu::PresentMode::VSync);
    }

    void Backend::InvalidateImage(uint32_t image)
    {
        _invalidatedImages.insert(image);
    }

    std::optional<Gpu::FrameHandle> Backend::BeginFrame(uint64_t frameNumber)
    {
        if (_activeToken.has_value())
        {
            throw std::logic_error("Vulkan backend already has an active frame");
        }

        _activeToken = _device.BeginFrame();
        if (!_activeToken.has_value())
        {
            return std::nullopt;
        }
        _palettePipeline.RefreshSwapchain(_device);

        _activeFrame = Gpu::FrameHandle{
            .frameNumber = frameNumber,
            .frameSlot = _activeToken->frameIndex,
            .imageIndex = _activeToken->imageIndex,
            .drawableExtent = { _activeToken->extent.width, _activeToken->extent.height },
        };
        _submitted = false;
        _latestTimings = Gpu::FrameTimings{ .frameNumber = frameNumber };
        return _activeFrame;
    }

    Gpu::UploadSlice Backend::AllocateUpload(uint64_t size, uint64_t alignment)
    {
        if (!_activeToken.has_value())
        {
            throw std::logic_error("Vulkan upload allocation requires an active frame");
        }
        const auto allocation = _activeToken->upload->Allocate(size, alignment);
        if (!allocation)
        {
            return {};
        }
        return {
            allocation.offset,
            std::span<std::byte>(allocation.data, static_cast<size_t>(allocation.size)),
        };
    }

    void Backend::SetPalette(std::span<const std::byte> rgba)
    {
        if (rgba.size() != _pendingPalette.size())
        {
            throw std::invalid_argument("GPU palettes must contain exactly 256 RGBA8 entries");
        }
        std::copy(rgba.begin(), rgba.end(), _pendingPalette.begin());
        _paletteDirty = true;
    }

    void Backend::Submit(const Gpu::FrameHandle& frame, const Gpu::FrameCommandStream& commands)
    {
        ValidateActiveFrame(frame);
        if (HasUnsupportedDrawCommands(commands))
        {
            throw std::logic_error(
                "Vulkan rectangle, transparency, and weather execution is incomplete; renderer selection remains disabled");
        }

        const auto start = Clock::now();
        RecordPendingPalette();
        RecordTextureUploads(commands);
        if (commands.lines.empty())
        {
            _resources.RecordCanvasClear(_activeToken->commandBuffer, _activeToken->frameIndex, 0);
        }
        else
        {
            _linePipeline.Record(*_activeToken, commands.lines);
        }
        _palettePipeline.Record(*_activeToken);
        _submitted = true;
        _latestTimings->cpuSubmitMicroseconds = ToMicroseconds(Clock::now() - start);
    }

    void Backend::Present(const Gpu::FrameHandle& frame)
    {
        ValidateActiveFrame(frame);
        if (!_submitted)
        {
            throw std::logic_error("Vulkan frame must be submitted before presentation");
        }

        const auto start = Clock::now();
        _device.EndFrame(*_activeToken);
        _latestTimings->cpuPresentMicroseconds = ToMicroseconds(Clock::now() - start);
        _activeToken.reset();
        _activeFrame.reset();
        _submitted = false;
    }

    std::optional<Gpu::FrameTimings> Backend::GetLatestTimings() const
    {
        return _latestTimings;
    }

    void Backend::RequestReadback(const Gpu::FrameHandle& frame, Gpu::ReadbackRequest)
    {
        ValidateActiveFrame(frame);
        throw std::logic_error("Asynchronous Vulkan readback is not implemented yet");
    }

    bool Backend::TryTakeReadback(uint64_t, std::span<std::byte>)
    {
        return false;
    }

    void Backend::WaitIdle()
    {
        _device.WaitIdle();
    }

    void Backend::ValidateActiveFrame(const Gpu::FrameHandle& frame) const
    {
        if (!_activeFrame.has_value() || !_activeToken.has_value() || _activeFrame->frameNumber != frame.frameNumber
            || _activeFrame->frameSlot != frame.frameSlot || _activeFrame->imageIndex != frame.imageIndex)
        {
            throw std::logic_error("GPU frame handle does not match the active Vulkan frame");
        }
    }

    void Backend::RecordPendingPalette()
    {
        if (!_paletteDirty)
        {
            return;
        }
        auto allocation = _activeToken->upload->Allocate(_pendingPalette.size(), alignof(uint32_t));
        if (!allocation)
        {
            throw std::runtime_error("Vulkan upload ring has no room for the palette");
        }
        std::memcpy(allocation.data, _pendingPalette.data(), _pendingPalette.size());
        _resources.RecordPaletteUpload(_activeToken->commandBuffer, allocation);
        _paletteDirty = false;
    }

    void Backend::RecordTextureUploads(const Gpu::FrameCommandStream& commands)
    {
        if (commands.textureUploads.empty())
        {
            return;
        }

        for (const auto& upload : commands.textureUploads)
        {
            const auto height = static_cast<uint32_t>(std::max(0, upload.bounds.w - upload.bounds.y));
            const auto size = static_cast<VkDeviceSize>(upload.sourcePitch) * height;
            if (upload.sourceOffset > _activeToken->upload->GetUsed()
                || size > _activeToken->upload->GetUsed() - upload.sourceOffset)
            {
                throw std::out_of_range("Vulkan texture upload references bytes outside the current upload ring");
            }
        }

        _resources.BeginAtlasUploads(_activeToken->commandBuffer);
        for (const auto& upload : commands.textureUploads)
        {
            const auto height = static_cast<uint32_t>(std::max(0, upload.bounds.w - upload.bounds.y));
            const auto size = static_cast<VkDeviceSize>(upload.sourcePitch) * height;
            const UploadAllocation allocation = {
                _activeToken->upload->GetBuffer(), upload.sourceOffset, size, nullptr
            };
            _resources.RecordAtlasUpload(
                _activeToken->commandBuffer, allocation, upload.atlas, upload.bounds, upload.sourcePitch);
            _invalidatedImages.erase(upload.image);
        }
        _resources.EndAtlasUploads(_activeToken->commandBuffer);
    }

    void Backend::PopulateCapabilities()
    {
        VkPhysicalDeviceProperties properties{};
        vkGetPhysicalDeviceProperties(_device.GetPhysicalDevice(), &properties);
        VkPhysicalDeviceMemoryProperties memory{};
        vkGetPhysicalDeviceMemoryProperties(_device.GetPhysicalDevice(), &memory);
        uint64_t deviceLocalMemory = 0;
        for (uint32_t i = 0; i < memory.memoryHeapCount; i++)
        {
            if ((memory.memoryHeaps[i].flags & VK_MEMORY_HEAP_DEVICE_LOCAL_BIT) != 0)
            {
                deviceLocalMemory += memory.memoryHeaps[i].size;
            }
        }

        _capabilities = {
            .api = Gpu::BackendApi::Vulkan,
            .framesInFlight = kFramesInFlight,
            .maxTextureDimension = properties.limits.maxImageDimension2D,
            .maxTextureArrayLayers = properties.limits.maxImageArrayLayers,
            .deviceLocalMemory = deviceLocalMemory,
            .uploadRingCapacity = _config.uploadRingBytesPerFrame,
            .supportsTimelineSemaphores = false,
            .supportsIndirectCount = false,
            .supportsDescriptorIndexing = false,
            .supportsPortabilitySubset = false,
            .supportsLineCommands = true,
            .supportsIndexedDrawCommands = false,
        };
    }

    std::unique_ptr<Gpu::Backend> CreateBackend()
    {
        return std::make_unique<Backend>();
    }
} // namespace OpenRCT2::Ui::Vulkan

#endif // ENABLE_VULKAN
