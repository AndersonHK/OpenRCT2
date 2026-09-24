/*****************************************************************************
 * Copyright (c) 2014-2026 OpenRCT2 developers
 * OpenRCT2 is licensed under the GNU General Public License version 3.
 *****************************************************************************/
#ifdef ENABLE_VULKAN
    #include "VulkanRenderService.h"

    #include "VulkanFrameExecutor.h"
    #include "VulkanImageAliasPipeline.h"
    #include "VulkanPalettePipeline.h"
    #include "VulkanSubmissionSlots.h"

    #include <algorithm>
    #include <cstring>
    #include <limits>
    #include <openrct2-renderer/gpu/GpuCommandDrawingContext.h>
    #include <openrct2/drawing/IDrawingEngine.h>
    #include <openrct2/drawing/RenderTarget.h>
    #include <unordered_set>

namespace OpenRCT2::Ui::Vulkan
{
    namespace
    {
        using namespace Drawing;
        [[noreturn]] void Fail(RenderErrorCode code, const char* message)
        {
            throw RenderServiceException({ code, message });
        }

        struct Job
        {
            OffscreenRenderRequest request;
            Gpu::FrameCommandStream commands;
            Gpu::AtlasResidencyToken residency{};
            std::shared_ptr<RenderCompletion> completion;
        };

        // This reusable domain has exactly one active slot and one target size.
        // All methods run on the service worker; only the shared queue/cache locks
        // cross into main rendering. No main recording state is touched.
        class AuxiliaryDomain
        {
            std::shared_ptr<DeviceContext> _context;
            SubmissionSlots _slots;
            FrameExecutor _executor;
            ImageAliasPipeline _alias;
            PalettePipeline _palette;
            Image _output;
            RenderExtent _logical{};
            RenderExtent _outputExtent{};
            uint64_t _outputGeneration = 0;
            RenderServiceOptions _options;

            UploadAllocation Readback(const SubmissionToken& token, const Image& image, VkImageLayout layout, uint32_t bpp)
            {
                const auto extent = image.GetExtent();
                const auto allocation = token.upload->Allocate(
                    static_cast<VkDeviceSize>(extent.width) * extent.height * bpp, 4);
                if (!allocation)
                    throw std::runtime_error("Auxiliary readback exceeds its bounded upload ring");
                const VkImageSubresourceRange range{ VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
                RecordImageBarrier(
                    token.commandBuffer, image.GetImage(), layout, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, range,
                    VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
                    VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT, VK_ACCESS_TRANSFER_READ_BIT);
                const VkBufferImageCopy copy{ .bufferOffset = allocation.offset,
                                              .imageSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 },
                                              .imageExtent = extent };
                vkCmdCopyImageToBuffer(
                    token.commandBuffer, image.GetImage(), VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, allocation.buffer, 1, &copy);
                const VkBufferMemoryBarrier host{ .sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER,
                                                  .srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT,
                                                  .dstAccessMask = VK_ACCESS_HOST_READ_BIT,
                                                  .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                                                  .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                                                  .buffer = allocation.buffer,
                                                  .offset = allocation.offset,
                                                  .size = allocation.size };
                vkCmdPipelineBarrier(
                    token.commandBuffer, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_HOST_BIT, 0, 0, nullptr, 1, &host, 0,
                    nullptr);
                RecordImageBarrier(
                    token.commandBuffer, image.GetImage(), VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, layout, range,
                    VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_ACCESS_TRANSFER_READ_BIT,
                    VK_ACCESS_MEMORY_READ_BIT);
                return allocation;
            }

        public:
            AuxiliaryDomain(std::shared_ptr<DeviceContext> context, RenderServiceOptions options)
                : _context(std::move(context))
                , _slots(_context, options.uploadBytes, 1)
                , _options(std::move(options))
            {
            }
            ~AuxiliaryDomain()
            {
                // A failed queue operation may have submitted before throwing.
                // Retain all resources until the shared device retires that work.
                _context->WaitIdle();
                _alias.Dispose();
                _palette.Dispose();
                _output.Dispose();
                _executor.Dispose();
            }
            RenderResult Execute(Job& job)
            {
                const auto& request = job.request;
                if (request.orderedAlias)
                {
                    _alias.Initialise(*_context, _options.shaderDirectory);
                    const auto token = *_slots.Begin(0, true);
                    bool submitted = false;
                    try
                    {
                        const auto indices = _alias.Record(token, request);
                        submitted = true;
                        _slots.Submit(token);
                        if (!_slots.Wait(token, UINT64_MAX))
                            Fail(RenderErrorCode::timeout, "Ordered bitmap alias did not retire");
                        token.upload->Invalidate(indices.offset, indices.size);
                        RenderResult result{ .identity = job.completion->GetIdentity(),
                                             .logicalExtent = request.logicalExtent,
                                             .outputExtent = request.outputExtent,
                                             .palette = request.palette };
                        result.indexed.assign(indices.data, indices.data + request.initialIndices.size());
                        return result;
                    }
                    catch (...)
                    {
                        if (!submitted)
                            _slots.Abandon(token);
                        else
                            _context->WaitIdle();
                        throw;
                    }
                }
                if (_logical.width == 0)
                {
                    _executor.Initialise(
                        _context, { request.logicalExtent.width, request.logicalExtent.height }, _options.shaderDirectory, 1,
                        _options.atlasLayers, false);
                    if (!_options.remapPalette.empty())
                        _executor.SetRemapPalette(_options.remapPalette);
                    if (!_options.blendPalette.empty())
                        _executor.SetBlendPalette(_options.blendPalette);
                    _palette.Initialise(*_context, _executor.GetResources(), _options.shaderDirectory);
                    _logical = request.logicalExtent;
                }
                else if (_logical != request.logicalExtent)
                {
                    _executor.Resize({ request.logicalExtent.width, request.logicalExtent.height });
                    _palette.RefreshDescriptors(_executor.GetResources());
                    _logical = request.logicalExtent;
                }
                if (request.rgbaOutput && _outputExtent != request.outputExtent)
                {
                    _palette.ReleaseSwapchainResources();
                    _output.Initialise(
                        _context->GetPhysicalDevice(), _context->GetDevice(),
                        { request.outputExtent.width, request.outputExtent.height, 1 }, 1, VK_FORMAT_R8G8B8A8_UNORM,
                        VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT, VK_IMAGE_ASPECT_COLOR_BIT);
                    _outputExtent = request.outputExtent;
                    const auto view = _output.GetView();
                    _palette.RefreshOutput(
                        VK_FORMAT_R8G8B8A8_UNORM, { _outputExtent.width, _outputExtent.height }, std::span(&view, 1),
                        ++_outputGeneration, 0, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);
                }
                auto palette = request.palette;
                for (size_t index = 0; index < palette.size(); ++index)
                {
                    if (request.alphaPolicy == RenderAlphaPolicy::opaque)
                        palette[index].alpha = 255;
                    else if (request.alphaPolicy == RenderAlphaPolicy::transparentIndexZero)
                        palette[index].alpha = index == 0 ? 0 : 255;
                }
                static_assert(sizeof(RenderColour) == 4);
                _executor.SetPalette(std::as_bytes(std::span(palette)));
                const auto token = *_slots.Begin(0, true);
                bool submitted = false;
                try
                {
                    const auto frame = _executor.Record(token, job.commands, request.clearIndex, request.initialIndices);
                    if (request.rgbaOutput)
                    {
                        _palette.SetCanvasSource(0, *frame.canvas);
                        const auto factor = std::max<uint32_t>(
                            1,
                            std::max(
                                (request.outputExtent.width + request.logicalExtent.width - 1) / request.logicalExtent.width,
                                (request.outputExtent.height + request.logicalExtent.height - 1)
                                    / request.logicalExtent.height));
                        _palette.Record(
                            token, 0, { request.outputExtent.width, request.outputExtent.height }, false,
                            { request.logicalExtent.width, request.logicalExtent.height },
                            { static_cast<Gpu::ScaleMode>(request.scaleQuality), factor });
                    }
                    UploadAllocation indices{}, rgba{};
                    if (request.indexedOutput)
                        indices = Readback(token, *frame.canvas, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, 1);
                    if (request.rgbaOutput)
                        rgba = Readback(token, _output, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, 4);
                    // Before Submit, exceptions may safely abandon. Submit errors
                    // require retirement, never a speculative command-pool reset.
                    submitted = true;
                    _slots.Submit(token);
                    _executor.Commit();
                    _slots.Wait(token, UINT64_MAX);
                    RenderResult result{ .identity = job.completion->GetIdentity(),
                                         .logicalExtent = request.logicalExtent,
                                         .outputExtent = request.outputExtent,
                                         .palette = request.palette };
                    const auto copy = [&](const UploadAllocation& allocation, std::vector<std::byte>& bytes) {
                        if (!allocation)
                            return;
                        token.upload->Invalidate(allocation.offset, allocation.size);
                        bytes.assign(allocation.data, allocation.data + static_cast<size_t>(allocation.size));
                    };
                    copy(indices, result.indexed);
                    copy(rgba, result.rgba);
                    return result;
                }
                catch (...)
                {
                    if (!submitted)
                    {
                        _slots.Abandon(token);
                        _executor.Discard(0);
                    }
                    else
                        _context->WaitIdle();
                    throw;
                }
            }
        };

        struct ServiceState
        {
            const std::thread::id owner = std::this_thread::get_id();
            RenderServiceOptions options;
            DeviceProvider provider;
            std::shared_ptr<Gpu::TextureCache> cache;
            std::mutex mutex;
            std::unordered_set<uint32_t> pendingInvalidations;
            std::condition_variable condition;
            std::unique_ptr<Job> queued;
            std::shared_ptr<RenderCompletion> outstanding;
            std::optional<RenderError> failure;
            bool stopping = false;
            bool busy = false;
            uint64_t nextSubmission = 0;
            explicit ServiceState(RenderServiceOptions config, DeviceProvider deviceProvider)
                : options(std::move(config))
                , provider(std::move(deviceProvider))
                , cache(std::make_shared<Gpu::TextureCache>(options.atlasLayers))
            {
            }
            void DrainImageInvalidations()
            {
                CheckOwner();
                // The owner holds the busy recording slot. Worker producers only append IDs; retirement cannot
                // admit another recorder. TextureCache preserves allocations already bound by this frame.
                const std::lock_guard lock(mutex);
                for (const auto image : pendingInvalidations)
                    cache->InvalidateImage(image);
                pendingInvalidations.clear();
            }
            void CheckOwner() const
            {
                if (std::this_thread::get_id() != owner)
                    Fail(RenderErrorCode::wrongThread, "Offscreen recording requires its owner thread");
            }
        };

        // Legacy Gfx helpers select their drawing context from the target's
        // engine. This view belongs to one session, never the main display.
        // Session Submit/Cancel exclusively own recording boundaries.
        class SessionEngine final : public IDrawingEngine
        {
            IRenderSession& _session;
            Gpu::TextureCache& _cache;
            [[noreturn]] static void Unsupported()
            {
                Fail(RenderErrorCode::invalidState, "Offscreen target engine lifetime is owned by its render session");
            }

        public:
            SessionEngine(IRenderSession& session, Gpu::TextureCache& cache)
                : _session(session)
                , _cache(cache)
            {
            }
            void Initialise() override
            {
                Unsupported();
            }
            void Resize(uint32_t, uint32_t) override
            {
                Unsupported();
            }
            void SetPalette(const GamePalette&) override
            {
                Unsupported();
            }
            void SetVSync(bool) override
            {
                Unsupported();
            }
            void Invalidate(int32_t, int32_t, int32_t, int32_t) override
            {
                // Complete auxiliary targets do not maintain display dirty blocks.
                static_cast<void>(_session.GetDrawingContext());
            }
            void BeginDraw() override
            {
                Unsupported();
            }
            void EndDraw() override
            {
                Unsupported();
            }
            void PaintWindows() override
            {
                Unsupported();
            }
            void PaintWeather() override
            {
                Unsupported();
            }
            void CopyRect(int32_t, int32_t, int32_t, int32_t, int32_t, int32_t) override
            {
                Unsupported();
            }
            std::string Screenshot() override
            {
                Unsupported();
            }
            IDrawingContext* GetDrawingContext() override
            {
                return &_session.GetDrawingContext();
            }
            RenderTarget* getRT() override
            {
                return &_session.GetRenderTarget();
            }
            DrawingEngineFlags GetFlags() override
            {
                return {};
            }
            void InvalidateImage(uint32_t image) override
            {
                static_cast<void>(_session.GetDrawingContext());
                _cache.InvalidateImage(image);
            }
        };

        class Session final : public IRenderSession
        {
            std::shared_ptr<ServiceState> _state;
            std::unique_ptr<Job> _job;
            std::vector<std::byte> _bits;
            RenderTarget _target;
            Gpu::CommandDrawingContext _drawing;
            SessionEngine _engine;
            bool _recording = false;
            void Check()
            {
                _state->CheckOwner();
                if (!_recording)
                    Fail(RenderErrorCode::invalidState, "Offscreen session is already sealed or cancelled");
                const std::lock_guard lock(_state->mutex);
                if (_state->stopping)
                    Fail(RenderErrorCode::shuttingDown, "Offscreen service is shutting down");
            }

        public:
            Session(std::shared_ptr<ServiceState> state, std::unique_ptr<Job> job)
                : _state(std::move(state))
                , _job(std::move(job))
                , _bits(
                      _job->request.orderedAlias
                          ? 0
                          : static_cast<size_t>(_job->request.logicalExtent.width) * _job->request.logicalExtent.height)
                , _target{ .bits = reinterpret_cast<PaletteIndex*>(_bits.data()),
                           .width = static_cast<int32_t>(_job->request.logicalExtent.width),
                           .height = static_cast<int32_t>(_job->request.logicalExtent.height) }
                , _drawing(_target, *_state->cache)
                , _engine(*this, *_state->cache)
            {
                _target.DrawingEngine = &_engine;
                _state->cache->BeginFrame();
                try
                {
                    _drawing.Begin(_job->commands);
                    _recording = true;
                }
                catch (...)
                {
                    _state->cache->AbortFrame();
                    throw;
                }
            }
            ~Session() override
            {
                Cancel();
            }
            IDrawingContext& GetDrawingContext() override
            {
                Check();
                if (_job->request.orderedAlias)
                    Fail(RenderErrorCode::invalidState, "Ordered bitmap alias cannot record drawing commands");
                _state->DrainImageInvalidations();
                return _drawing;
            }
            RenderTarget& GetRenderTarget() override
            {
                Check();
                if (_job->request.orderedAlias)
                    Fail(RenderErrorCode::invalidState, "Ordered bitmap alias has no borrowed drawing target");
                _state->DrainImageInvalidations();
                return _target;
            }
            std::shared_ptr<IRenderCompletion> Submit() override
            {
                Check();
                _drawing.End();
                try
                {
                    _job->residency = _state->cache->SealFrame(_job->commands);
                }
                catch (...)
                {
                    Cancel();
                    throw;
                }
                _recording = false;
                const auto completion = _job->completion;
                {
                    const std::lock_guard lock(_state->mutex);
                    if (_state->stopping)
                    {
                        _state->cache->RetireFrame(_job->residency, Gpu::FrameRetirement::Failed);
                        _state->busy = false;
                        completion->Fail({ RenderErrorCode::shuttingDown, "Offscreen service stopped before submission" });
                        return completion;
                    }
                    _state->queued = std::move(_job);
                }
                _state->condition.notify_one();
                return completion;
            }
            void Cancel() noexcept override
            {
                if (!_recording)
                    return;
                _recording = false;
                // Session recording itself is owner-thread-affine. No GPU work
                // exists yet; release the recording lease exactly once.
                try
                {
                    if (_drawing.IsActive())
                        _drawing.End();
                    _state->cache->AbortFrame();
                }
                catch (...)
                {
                }
                _job->completion->Cancel();
                const std::lock_guard lock(_state->mutex);
                _state->busy = false;
                _state->outstanding.reset();
            }
        };

        class Service final : public IRenderService
        {
            std::shared_ptr<ServiceState> _state;
            std::thread _worker;
            std::mutex _shutdownMutex;
            static void Run(const std::shared_ptr<ServiceState>& state)
            {
                std::unique_ptr<AuxiliaryDomain> domain;
                for (;;)
                {
                    std::unique_ptr<Job> job;
                    bool stopped = false;
                    {
                        std::unique_lock lock(state->mutex);
                        state->condition.wait(lock, [&] { return state->stopping || state->queued != nullptr; });
                        if (!state->queued)
                            break;
                        job = std::move(state->queued);
                        stopped = state->stopping;
                    }
                    std::optional<RenderResult> result;
                    std::optional<RenderError> error;
                    try
                    {
                        if (stopped)
                            Fail(RenderErrorCode::shuttingDown, "Offscreen service stopped before recording GPU work");
                        if (!domain)
                        {
                            auto context = state->provider();
                            if (!context)
                                Fail(RenderErrorCode::unavailable, "The shared Vulkan device is unavailable");
                            domain = std::make_unique<AuxiliaryDomain>(std::move(context), state->options);
                        }
                        result = domain->Execute(*job);
                    }
                    catch (const RenderServiceException& e)
                    {
                        error = RenderError{ e.GetCode(), e.what() };
                    }
                    catch (const std::exception& e)
                    {
                        error = RenderError{ RenderErrorCode::executionFailed, e.what() };
                    }
                    catch (...)
                    {
                        error = RenderError{ RenderErrorCode::executionFailed, "Unknown Vulkan offscreen failure" };
                    }
                    // Retire before result delivery and before admitting another
                    // recording. Cancelled deliveries still own leases until here.
                    state->cache->RetireFrame(
                        job->residency, result ? Gpu::FrameRetirement::Presented : Gpu::FrameRetirement::Failed);
                    {
                        const std::lock_guard lock(state->mutex);
                        // Serialize terminal delivery with Shutdown before removing
                        // the outstanding job. Both take state then completion;
                        // completion never calls into the service or gameplay.
                        if (error)
                            job->completion->Fail(*error);
                        else
                            job->completion->Complete(std::move(*result));
                        state->busy = false;
                        state->outstanding.reset();
                        if (error)
                            state->failure = error; // Failed domains are not silently reused.
                    }
                }
            }

        public:
            Service(RenderServiceOptions options, DeviceProvider provider)
                : _state(std::make_shared<ServiceState>(std::move(options), std::move(provider)))
                , _worker([state = _state] { Run(state); })
            {
            }
            ~Service() override
            {
                Shutdown();
            }
            std::unique_ptr<IRenderSession> BeginOffscreen(OffscreenRenderRequest request) override
            {
                _state->CheckOwner();
                ValidateOffscreenRenderRequest(request);
                if (request.lightingEnabled)
                    Fail(RenderErrorCode::unavailable, "Offscreen light snapshot injection is not yet implemented");
                if (static_cast<uint64_t>(request.logicalExtent.width) * request.logicalExtent.height
                        > _state->options.maxTargetPixels
                    || static_cast<uint64_t>(request.outputExtent.width) * request.outputExtent.height
                        > _state->options.maxTargetPixels)
                    Fail(RenderErrorCode::invalidRequest, "Offscreen target exceeds the configured bounded target pool");
                auto job = std::make_unique<Job>();
                job->request = std::move(request);
                {
                    const std::lock_guard lock(_state->mutex);
                    if (_state->stopping)
                        Fail(RenderErrorCode::shuttingDown, "Offscreen service is shutting down");
                    if (_state->failure)
                        throw RenderServiceException(*_state->failure);
                    if (_state->busy)
                        Fail(
                            RenderErrorCode::invalidState,
                            "The bounded auxiliary domain already has a recording or submission");
                    if (_state->nextSubmission == std::numeric_limits<uint64_t>::max())
                        Fail(RenderErrorCode::invalidState, "Offscreen identity exhausted");
                    const auto sequence = ++_state->nextSubmission;
                    job->completion = std::make_shared<RenderCompletion>(
                        RenderSubmissionIdentity{ sequence, 1, sequence, job->request.name }, job->request, _worker.get_id());
                    _state->outstanding = job->completion;
                    _state->busy = true;
                }
                try
                {
                    _state->DrainImageInvalidations();
                    return std::make_unique<Session>(_state, std::move(job));
                }
                catch (...)
                {
                    const std::lock_guard lock(_state->mutex);
                    _state->busy = false;
                    _state->outstanding.reset();
                    throw;
                }
            }
            void InvalidateImage(uint32_t image) override
            {
                const std::lock_guard lock(_state->mutex);
                if (!_state->stopping)
                    _state->pendingInvalidations.insert(image);
            }
            void Shutdown() noexcept override
            {
                const std::lock_guard shutdownLock(_shutdownMutex);
                {
                    const std::lock_guard lock(_state->mutex);
                    _state->stopping = true;
                    if (_state->outstanding)
                        _state->outstanding->Fail({ RenderErrorCode::shuttingDown, "Offscreen service is shutting down" });
                }
                _state->condition.notify_one();
                if (_worker.joinable())
                    _worker.join();
                // A cancelled recorder may retain ServiceState after Context teardown.
                // It must not keep the presentation device/loader alive past SDL video.
                _state->provider = {};
            }
        };

        class Factory final : public IRenderServiceFactory
        {
            RenderServiceOptions _options;
            DeviceProvider _provider;

        public:
            Factory(RenderServiceOptions options, DeviceProvider provider)
                : _options(std::move(options))
                , _provider(std::move(provider))
            {
            }
            std::unique_ptr<IRenderService> Create() override
            {
                return std::make_unique<Service>(_options, _provider);
            }
        };
    } // namespace

    std::shared_ptr<Drawing::IRenderServiceFactory> CreateRenderServiceFactory(
        RenderServiceOptions options, DeviceProvider provider)
    {
        if (!provider || options.shaderDirectory.empty() || options.atlasLayers == 0 || options.atlasLayers > Gpu::kAtlasLayers
            || options.uploadBytes < 1024 * 1024 || options.maxTargetPixels == 0
            || (!options.remapPalette.empty() && options.remapPalette.size() != 256 * 256)
            || (!options.blendPalette.empty() && options.blendPalette.size() != 256 * 256))
            throw Drawing::RenderServiceException(
                { Drawing::RenderErrorCode::invalidRequest, "Invalid Vulkan service configuration" });
        return std::make_shared<Factory>(std::move(options), std::move(provider));
    }
    std::shared_ptr<Drawing::IRenderServiceFactory> CreateGraphicsOnlyRenderServiceFactory(RenderServiceOptions options)
    {
        return CreateRenderServiceFactory(std::move(options), [] { return DeviceContext::CreateGraphicsOnly(); });
    }
} // namespace OpenRCT2::Ui::Vulkan
#endif
