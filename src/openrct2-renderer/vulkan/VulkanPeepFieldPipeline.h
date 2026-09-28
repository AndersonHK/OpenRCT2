// Copyright (c) 2014-2026 OpenRCT2 developers. GPL-3.0-or-later.
#pragma once
#ifdef ENABLE_VULKAN
    #include "VulkanDevice.h"
    #include "VulkanResources.h"

    #include <array>
    #include <filesystem>
    #include <openrct2-renderer/gpu/PeepAssetGeneration.h>
    #include <openrct2/drawing/RetainedPeepState.h>
    #include <optional>

namespace OpenRCT2::Ui::Vulkan
{
    // Owned by the common terrain/entity pipeline, using its device and submission queue.
    // Resident field buffers are consumed by common column generation, never a separate overlay.
    class PeepFieldPipeline final
    {
        VkDevice _device{};
        VkDescriptorSetLayout _descriptorLayout{};
        VkDescriptorPool _pool{};
        VkDescriptorSet _set{};
        VkPipelineLayout _layout{};
        VkPipeline _pipeline{};
        Buffer _fields, _updates, _bins, _catalog;
        Drawing::RetainedPeepFieldConsumer _consumer;
        std::optional<Drawing::RetainedPeepFieldDelta> _pending;
        std::shared_ptr<const Gpu::PeepAssetGeneration> _residentAssets, _pendingAssets;
        const SubmissionSlots* _recordedOwner{};
        uint64_t _recordedToken{};
        uint32_t _recordedSlot{};
        uint64_t _uploadBytes{};

    public:
        static constexpr uint32_t kCapacity = kMaxEntities;
        static constexpr uint32_t kFactCapacity = 64 * 256 * Drawing::kRetainedPeepAnimationTypes;
        static constexpr uint32_t kDescriptorCapacity = UINT16_MAX;
        static constexpr uint32_t kBinWords = 2048 + 1024 * 2048 + kCapacity;
        static constexpr uint32_t kCatalogWords = 4 + 8 * kDescriptorCapacity + 4 * kFactCapacity;
        ~PeepFieldPipeline()
        {
            Dispose();
        }
        void Initialise(const DeviceContext&, const std::filesystem::path& shader);
        void Dispose(); // Owning executor retires all submissions first.
        void Record(
            const SubmissionToken&, std::shared_ptr<const Drawing::RetainedPeepSnapshot>,
            std::shared_ptr<const Gpu::PeepAssetGeneration>, bool buildTileBins = true);
        void Commit(); // Only after accepted submission; never after mere recording.
        void Discard() noexcept;
        const Buffer& GetFields() const noexcept
        {
            return _fields;
        }
        const Buffer& GetBins() const noexcept
        {
            return _bins;
        }
        const Buffer& GetCatalog() const noexcept
        {
            return _catalog;
        }
        uint64_t GetLastUploadBytes() const noexcept
        {
            return _uploadBytes;
        }
    };
} // namespace OpenRCT2::Ui::Vulkan
#endif
