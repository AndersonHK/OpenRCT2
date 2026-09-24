/*****************************************************************************
 * Copyright (c) 2014-2026 OpenRCT2 developers
 * OpenRCT2 is licensed under the GNU General Public License version 3.
 *****************************************************************************/

#pragma once

#include <array>
#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <memory>
#include <mutex>
#include <optional>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

namespace OpenRCT2::Drawing
{
    struct IDrawingContext;
    struct RenderTarget;

    struct RenderExtent
    {
        uint32_t width{};
        uint32_t height{};
        bool operator==(const RenderExtent&) const = default;
    };

    struct RenderColour
    {
        uint8_t red{}, green{}, blue{}, alpha{};
        bool operator==(const RenderColour&) const = default;
    };
    using RenderPalette = std::array<RenderColour, 256>;

    enum class RenderAlphaPolicy : uint8_t
    {
        // All output alpha bytes are 255, independent of palette alpha.
        opaque,
        // Preserve the supplied palette alpha at every index.
        paletteAlpha,
        // Index zero is transparent; all other indices are opaque.
        transparentIndexZero,
    };
    enum class RenderScaleQuality : uint8_t
    {
        nearest,
        linear,
        smoothNearest,
    };
    enum class RenderInitialContents : uint8_t
    {
        clearIndex,
        ownedIndices,
    };

    // Auxiliary bitmap compatibility only: scan the clipped source/destination rectangle in row-major order
    // against the SAME mutable indexed allocation. This is not a world rendering command or a sprite snapshot.
    // CPU owns geometry and the legacy lookup table; only GPU execution reads/writes pixels.
    struct OrderedImageAlias
    {
        uint32_t sourceX{}, sourceY{}, destinationX{}, destinationY{}, width{}, height{};
        bool skipSourceZero{};
        bool skipMappedZero{};
        std::array<uint8_t, 256> remap{};
    };
    constexpr uint32_t kOrderedImageAliasMaxPixels = 4 * 1024 * 1024;
    constexpr uint32_t kOrderedImageAliasSlicePixels = 8192;

    struct OffscreenRenderRequest
    {
        std::string name;
        RenderExtent logicalExtent;
        RenderExtent outputExtent;
        RenderInitialContents initialContents{ RenderInitialContents::clearIndex };
        uint8_t clearIndex{};
        std::vector<std::byte> initialIndices;
        RenderPalette palette{};
        RenderAlphaPolicy alphaPolicy{ RenderAlphaPolicy::transparentIndexZero };
        RenderScaleQuality scaleQuality{ RenderScaleQuality::nearest };
        bool indexedOutput{ true };
        bool rgbaOutput{};
        bool lightingEnabled{};
        // When present, this is the entire operation. Session drawing/target access is forbidden.
        std::optional<OrderedImageAlias> orderedAlias;
        // Auxiliary painting uses isolated, synchronously captured assets/world state. It must never publish a temporary
        // preview world into the main presentation generation. All dependencies become owned/immutable before Submit returns.
    };

    struct RenderSubmissionIdentity
    {
        // IDs are nonzero and unique within the service lifetime. Reusing a pooled target increments its generation.
        uint64_t submissionId{};
        uint64_t targetId{};
        uint64_t targetGeneration{};
        std::string name;
        bool operator==(const RenderSubmissionIdentity&) const = default;
    };

    enum class RenderErrorCode : uint8_t
    {
        unavailable,
        invalidRequest,
        invalidState,
        wrongThread,
        creationFailed,
        cancelled,
        timeout,
        staleTarget,
        deviceLost,
        shuttingDown,
        executionFailed,
    };

    struct RenderError
    {
        RenderErrorCode code;
        std::string message;
    };

    class RenderServiceException : public std::runtime_error
    {
    public:
        explicit RenderServiceException(RenderError error);
        RenderErrorCode GetCode() const noexcept;

    private:
        RenderErrorCode _code;
    };

    struct RenderResult
    {
        RenderSubmissionIdentity identity;
        RenderExtent logicalExtent;
        RenderExtent outputExtent;
        RenderPalette palette{};
        // Both outputs are tightly packed and top-down. RGBA is straight-alpha byte-space SDR RGBA8, not premultiplied.
        // Indexed bytes cover logicalExtent; RGBA bytes cover outputExtent. Unrequested outputs must be empty.
        std::vector<std::byte> indexed;
        std::vector<std::byte> rgba;
    };

    struct RenderOutcome
    {
        RenderSubmissionIdentity identity;
        std::shared_ptr<const RenderResult> result;
        std::optional<RenderError> error;
    };

    void ValidateOffscreenRenderRequest(const OffscreenRenderRequest& request);

    struct IRenderCompletion
    {
        virtual ~IRenderCompletion() = default;
        virtual const RenderSubmissionIdentity& GetIdentity() const noexcept = 0;
        // A timeout is a nonterminal observation. The caller may wait again; no in-flight resource is reclaimed.
        // Service implementations must reject waits from their submission worker to avoid self-deadlock.
        virtual RenderOutcome Wait(std::chrono::milliseconds timeout) = 0;
        // Cancellation suppresses result delivery. It does not cancel submitted GPU work or release its leases.
        virtual void Cancel() = 0;
    };

    // Common completion state for implementations and contract tests. The submission job, not this delivery object, owns
    // GPU fences/resources/leases until completion. Dropping or cancelling this object must not free in-flight resources.
    class RenderCompletion final : public IRenderCompletion
    {
    public:
        RenderCompletion(
            RenderSubmissionIdentity identity, const OffscreenRenderRequest& request, std::thread::id submissionWorker = {});
        const RenderSubmissionIdentity& GetIdentity() const noexcept override;
        RenderOutcome Wait(std::chrono::milliseconds timeout) override;
        void Cancel() override;
        bool Complete(RenderResult result);
        bool Fail(RenderError error);

    private:
        const RenderSubmissionIdentity _identity;
        const RenderExtent _logicalExtent;
        const RenderExtent _outputExtent;
        const bool _indexedOutput;
        const bool _rgbaOutput;
        const std::thread::id _submissionWorker;
        std::mutex _mutex;
        std::condition_variable _condition;
        std::optional<RenderOutcome> _outcome;
    };

    struct IRenderSession
    {
        virtual ~IRenderSession() = default;
        // Recording is game/UI-thread-affine and valid only until Submit/Cancel. The target must supply owned addressable
        // bits for legacy clipping arithmetic even though rasterization is performed by the renderer.
        virtual IDrawingContext& GetDrawingContext() = 0;
        virtual RenderTarget& GetRenderTarget() = 0;
        // Submit exactly once; seals owned commands/uploads/snapshots/leases. Auxiliary jobs are reliable, never superseded.
        virtual std::shared_ptr<IRenderCompletion> Submit() = 0;
        // Cancel or destruction before Submit releases recording leases once; after Submit the job owns them independently.
        virtual void Cancel() noexcept = 0;
    };

    struct IRenderService
    {
        virtual ~IRenderService() = default;
        virtual std::unique_ptr<IRenderSession> BeginOffscreen(OffscreenRenderRequest request) = 0;
        // Thread-safe notification, including viewport preparation workers. Cached implementations queue changes and
        // apply them when the owner acquires its recording context/target; submitted jobs retain their owned assets.
        // Reacquire a session accessor after mutation, rather than retaining a drawing context across asset changes.
        // Implementations must not call back into context ownership from this notification.
        // Stateless implementations need no notification work.
        virtual void InvalidateImage(uint32_t) {}
        // Reject new work, fail outstanding completions, and drain submitted resources before returning. Idempotent.
        // No gameplay/UI/script callbacks may run from GPU completion or shutdown.
        virtual void Shutdown() noexcept = 0;
    };

    struct IRenderServiceFactory
    {
        virtual ~IRenderServiceFactory() = default;
        // Application composition supplies presentation requirements before injection. Core knows no SDL/Vulkan types.
        // Owner-thread selection only; never initialise assets, a loader or a
        // device. Diagnostic factories remain explicitly enabled by default.
        virtual bool IsEnabled() const
        {
            return true;
        }
        virtual std::unique_ptr<IRenderService> Create() = 0;
    };

    // Context-thread-affine lazy ownership. Construction, status checks and shutdown never invoke an unused factory.
    // Failed creation is retained, so subsequent image requests receive the same failure rather than retrying device setup.
    class LazyRenderService final
    {
    public:
        explicit LazyRenderService(std::shared_ptr<IRenderServiceFactory> factory = {});
        ~LazyRenderService();
        IRenderService& Get();
        bool IsCreated() const noexcept;
        void InvalidateImage(uint32_t image);
        void Shutdown() noexcept;

    private:
        mutable std::mutex _notificationMutex;
        std::shared_ptr<IRenderServiceFactory> _factory;
        std::unique_ptr<IRenderService> _service;
        std::exception_ptr _failure;
        const std::thread::id _ownerThread;
        bool _creating{};
        bool _shutdown{};
    };
} // namespace OpenRCT2::Drawing
