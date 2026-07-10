/*****************************************************************************
 * Copyright (c) 2014-2026 OpenRCT2 developers
 *
 * For a complete list of all authors, please refer to contributors.md
 * Interested in contributing? Visit https://github.com/OpenRCT2/OpenRCT2
 *
 * OpenRCT2 is licensed under the GNU General Public License version 3.
 *****************************************************************************/

#pragma once

#ifdef ENABLE_VULKAN

    #include "VulkanDevice.h"
    #include "VulkanLinePipeline.h"
    #include "VulkanPalettePipeline.h"
    #include "VulkanResources.h"

    #include "../gpu/GpuBackend.h"

    #include <array>
    #include <memory>
    #include <optional>
    #include <unordered_set>

namespace OpenRCT2::Ui::Vulkan
{
    /**
     * Vulkan ownership and frame-lifetime implementation of the API-neutral
     * GPU backend. The resource upload and indexed presentation surface is
     * complete; gameplay selection remains gated until the sprite/line/
     * transparency command executor reports supportsIndexedDrawCommands.
     */
    class Backend final : public Gpu::Backend
    {
    private:
        Gpu::BackendConfig _config;
        Gpu::BackendCapabilities _capabilities;
        Device _device;
        IndexedResources _resources;
        LinePipeline _linePipeline;
        PalettePipeline _palettePipeline;
        std::optional<FrameToken> _activeToken;
        std::optional<Gpu::FrameHandle> _activeFrame;
        std::optional<Gpu::FrameTimings> _latestTimings;
        std::array<std::byte, 256 * 4> _pendingPalette{};
        bool _paletteDirty = true;
        bool _submitted = false;
        std::unordered_set<uint32_t> _invalidatedImages;

    public:
        Backend() = default;
        ~Backend() override;

        void Initialise(const Gpu::BackendConfig& config) override;
        void Dispose() override;
        [[nodiscard]] const Gpu::BackendCapabilities& GetCapabilities() const noexcept override;

        void Resize(Gpu::Extent logicalExtent) override;
        void SetPresentMode(Gpu::PresentMode mode) override;
        void InvalidateImage(uint32_t image) override;

        [[nodiscard]] std::optional<Gpu::FrameHandle> BeginFrame(uint64_t frameNumber) override;
        [[nodiscard]] Gpu::UploadSlice AllocateUpload(uint64_t size, uint64_t alignment) override;
        void SetPalette(std::span<const std::byte> rgba) override;
        void Submit(const Gpu::FrameHandle& frame, const Gpu::FrameCommandStream& commands) override;
        void Present(const Gpu::FrameHandle& frame) override;
        [[nodiscard]] std::optional<Gpu::FrameTimings> GetLatestTimings() const override;

        void RequestReadback(const Gpu::FrameHandle& frame, Gpu::ReadbackRequest request) override;
        [[nodiscard]] bool TryTakeReadback(uint64_t requestId, std::span<std::byte> destination) override;
        void WaitIdle() override;

    private:
        void ValidateActiveFrame(const Gpu::FrameHandle& frame) const;
        void RecordPendingPalette();
        void RecordTextureUploads(const Gpu::FrameCommandStream& commands);
        void PopulateCapabilities();
    };

    [[nodiscard]] std::unique_ptr<Gpu::Backend> CreateBackend();
} // namespace OpenRCT2::Ui::Vulkan

#endif // ENABLE_VULKAN
