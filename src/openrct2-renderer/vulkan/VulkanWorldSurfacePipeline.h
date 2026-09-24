/*****************************************************************************
 * Copyright (c) 2014-2026 OpenRCT2 developers
 *
 * OpenRCT2 is licensed under the GNU General Public License version 3.
 *****************************************************************************/

#pragma once

#ifdef ENABLE_VULKAN

    #include "VulkanDevice.h"
    #include "VulkanResources.h"
    #include "VulkanWorldFilterCompositor.h"

    #include <array>
    #include <filesystem>
    #include <openrct2-renderer/gpu/GpuCommandStream.h>
    #include <vector>

namespace OpenRCT2::Ui::Vulkan
{
    class WorldSurfacePipeline final
    {
    private:
        VkDevice _device = VK_NULL_HANDLE;
        uint32_t _frameCount = kFramesInFlight;
        VkPipelineCache _pipelineCache = VK_NULL_HANDLE;
        VkDescriptorSetLayout _descriptorSetLayout = VK_NULL_HANDLE;
        VkDescriptorPool _descriptorPool = VK_NULL_HANDLE;
        VkDescriptorSet _descriptorSet = VK_NULL_HANDLE;
        VkPipelineLayout _pipelineLayout = VK_NULL_HANDLE;
        VkRenderPass _renderPass = VK_NULL_HANDLE;
        VkPipeline _computePipeline = VK_NULL_HANDLE;
        VkPipeline _pipeline = VK_NULL_HANDLE;
        VkPipeline _filterPipeline = VK_NULL_HANDLE;
        std::array<VkFramebuffer, kFramesInFlight> _framebuffers{};
        Buffer _sourceRecords;
        Buffer _pathRecords;
        Buffer _objectRecords;
        Buffer _propCatalog;
        Buffer _trackCatalog;
        Buffer _flatRideCatalog;
        Buffer _entranceCatalog;
        Buffer _ridePoses;
        std::shared_ptr<const WorldRidePoseSnapshot> _uploadedRidePoses;
        bool _ridePosesInitialised{};
        Buffer _selection;
        Buffer _bannerTexts;
        std::shared_ptr<const Gpu::WorldBannerTextData> _uploadedBannerTexts;
        uint32_t _uploadedTextDefault{ UINT32_MAX };
        bool _bannerTextsInitialised{};
        std::shared_ptr<const std::vector<uint32_t>> _uploadedSelection;
        bool _selectionInitialised{};
        Buffer _spriteSets;
        Buffer _catalog;
        Buffer _prefixes;
        Buffer _status;
        WorldFilterCompositor _filters;
        Buffer _visibleRecords;
        Buffer _selectedVehicle;
        std::shared_ptr<const Gpu::SelectedVehiclePaintPacket> _uploadedSelectedVehicle;
        bool _selectedVehicleInitialised{};
        Buffer _indirectCommands;
        std::vector<uint64_t> _uploadedRevisions;
        std::vector<uint32_t> _pathOffsets, _pathCapacities;
        uint32_t _pathArenaEnd{};
        std::vector<uint32_t> _objectOffsets, _objectCapacities;
        uint32_t _objectArenaEnd{};
        uint64_t _uploadedSpriteRevision{};
        uint64_t _uploadedEpoch{};
        uint32_t _uploadedWidth{};
        uint32_t _uploadedHeight{};
        VkExtent2D _extent{};
        std::filesystem::path _shaderDirectory;
        static constexpr uint32_t kProfilePointCount = 3;
        VkQueryPool _profileQueries = VK_NULL_HANDLE;
        bool _profileRequested{};
        uint32_t _profileValidBits{};
        double _profilePeriodNs{};
        std::array<bool, kFramesInFlight> _profilePending{};
        uint64_t _profileSamples{}, _profileUnavailable{}, _profileDiscarded{};
        std::array<double, kProfilePointCount - 1> _profileTotalUs{}, _profileMaxUs{};

    public:
        WorldSurfacePipeline() = default;
        ~WorldSurfacePipeline();

        WorldSurfacePipeline(const WorldSurfacePipeline&) = delete;
        WorldSurfacePipeline& operator=(const WorldSurfacePipeline&) = delete;

        void Initialise(const DeviceContext& device, const IndexedResources& resources, std::filesystem::path shaderDirectory);
        void Dispose();
        void Record(const SubmissionToken& frame, const Gpu::WorldSurfaceSceneCommand& scene);
        void DiscardPendingUploads(uint32_t frameIndex = kFramesInFlight) noexcept;
        // Called only after the existing submission fence retires; never waits.
        void CompleteProfile(uint32_t frameIndex);
        [[nodiscard]] const Buffer& GetStatusBuffer() const noexcept
        {
            return _status;
        }

    private:
        void CreateDescriptors(const IndexedResources& resources);
        void CreateRenderPass();
        void CreatePipeline();
        void CreateFramebuffers(const IndexedResources& resources);
        void InitialiseProfile(const DeviceContext& device);
        void ProfilePoint(const SubmissionToken& frame, uint32_t point) const;
        void DisposeProfile();
    };
} // namespace OpenRCT2::Ui::Vulkan

#endif // ENABLE_VULKAN
