/*****************************************************************************
 * Copyright (c) 2014-2026 OpenRCT2 developers
 * OpenRCT2 is licensed under the GNU General Public License version 3.
 *****************************************************************************/

#include "RenderService.h"

#include <limits>
#include <utility>

namespace OpenRCT2::Drawing
{
    namespace
    {
        size_t ImageBytes(RenderExtent extent, size_t bytesPerPixel)
        {
            if (extent.width == 0 || extent.height == 0
                || extent.width > static_cast<uint32_t>(std::numeric_limits<int32_t>::max())
                || extent.height > static_cast<uint32_t>(std::numeric_limits<int32_t>::max())
                || static_cast<size_t>(extent.width) > std::numeric_limits<size_t>::max() / extent.height / bytesPerPixel)
                throw RenderServiceException({ RenderErrorCode::invalidRequest, "Invalid or overflowing render extent" });
            return static_cast<size_t>(extent.width) * extent.height * bytesPerPixel;
        }
    } // namespace

    RenderServiceException::RenderServiceException(RenderError error)
        : std::runtime_error(std::move(error.message))
        , _code(error.code)
    {
    }

    RenderErrorCode RenderServiceException::GetCode() const noexcept
    {
        return _code;
    }

    void ValidateOffscreenRenderRequest(const OffscreenRenderRequest& request)
    {
        const auto indexedBytes = ImageBytes(request.logicalExtent, 1);
        ImageBytes(request.outputExtent, 4);
        if (request.name.empty() || (!request.indexedOutput && !request.rgbaOutput))
            throw RenderServiceException({ RenderErrorCode::invalidRequest, "Render request needs a name and output" });
        if (request.initialContents == RenderInitialContents::ownedIndices)
        {
            if (request.initialIndices.size() != indexedBytes)
                throw RenderServiceException(
                    { RenderErrorCode::invalidRequest, "Initial indices must cover the logical target" });
        }
        else if (request.initialContents != RenderInitialContents::clearIndex || !request.initialIndices.empty())
            throw RenderServiceException({ RenderErrorCode::invalidRequest, "Invalid initial render contents" });
        if (request.alphaPolicy != RenderAlphaPolicy::opaque && request.alphaPolicy != RenderAlphaPolicy::paletteAlpha
            && request.alphaPolicy != RenderAlphaPolicy::transparentIndexZero)
            throw RenderServiceException({ RenderErrorCode::invalidRequest, "Unknown render alpha policy" });
        if (request.scaleQuality != RenderScaleQuality::nearest && request.scaleQuality != RenderScaleQuality::linear
            && request.scaleQuality != RenderScaleQuality::smoothNearest)
            throw RenderServiceException({ RenderErrorCode::invalidRequest, "Unknown render scaling policy" });
        if (request.orderedAlias)
        {
            const auto& alias = *request.orderedAlias;
            const auto inside = [](uint32_t start, uint32_t length, uint32_t limit) {
                return length != 0 && start < limit && length <= limit - start;
            };
            if (request.initialContents != RenderInitialContents::ownedIndices || !request.indexedOutput
                || request.rgbaOutput || request.lightingEnabled || request.outputExtent != request.logicalExtent
                || indexedBytes > kOrderedImageAliasMaxPixels
                || !inside(alias.sourceX, alias.width, request.logicalExtent.width)
                || !inside(alias.destinationX, alias.width, request.logicalExtent.width)
                || !inside(alias.sourceY, alias.height, request.logicalExtent.height)
                || !inside(alias.destinationY, alias.height, request.logicalExtent.height))
                throw RenderServiceException({ RenderErrorCode::invalidRequest, "Invalid bounded ordered bitmap alias" });
        }
    }

    RenderCompletion::RenderCompletion(
        RenderSubmissionIdentity identity, const OffscreenRenderRequest& request, std::thread::id submissionWorker)
        : _identity(std::move(identity))
        , _logicalExtent(request.logicalExtent)
        , _outputExtent(request.outputExtent)
        , _indexedOutput(request.indexedOutput)
        , _rgbaOutput(request.rgbaOutput)
        , _submissionWorker(submissionWorker)
    {
        ValidateOffscreenRenderRequest(request);
        if (_identity.submissionId == 0 || _identity.targetId == 0 || _identity.targetGeneration == 0
            || _identity.name != request.name)
            throw RenderServiceException({ RenderErrorCode::invalidRequest, "Invalid named render submission identity" });
    }

    const RenderSubmissionIdentity& RenderCompletion::GetIdentity() const noexcept
    {
        return _identity;
    }

    RenderOutcome RenderCompletion::Wait(std::chrono::milliseconds timeout)
    {
        std::unique_lock lock(_mutex);
        if (!_outcome && std::this_thread::get_id() == _submissionWorker)
            return { _identity,
                     {},
                     RenderError{ RenderErrorCode::wrongThread, "Submission worker cannot wait for its own render job" } };
        if (!_condition.wait_for(lock, timeout, [&]() { return _outcome.has_value(); }))
            return { _identity, {}, RenderError{ RenderErrorCode::timeout, "Timed out waiting for named render submission" } };
        return *_outcome;
    }

    void RenderCompletion::Cancel()
    {
        Fail({ RenderErrorCode::cancelled, "Render result delivery cancelled" });
    }

    bool RenderCompletion::Fail(RenderError error)
    {
        std::lock_guard lock(_mutex);
        if (_outcome.has_value())
            return false;
        _outcome = RenderOutcome{ _identity, {}, std::move(error) };
        _condition.notify_all();
        return true;
    }

    bool RenderCompletion::Complete(RenderResult result)
    {
        if (result.identity != _identity)
            return Fail({ RenderErrorCode::staleTarget, "Render result belongs to another submission or target generation" });
        if (result.logicalExtent != _logicalExtent || result.outputExtent != _outputExtent
            || result.indexed.size() != (_indexedOutput ? ImageBytes(_logicalExtent, 1) : 0)
            || result.rgba.size() != (_rgbaOutput ? ImageBytes(_outputExtent, 4) : 0))
            return Fail(
                { RenderErrorCode::executionFailed, "Render result has incorrect extents or owned output byte counts" });
        auto owned = std::make_shared<const RenderResult>(std::move(result));
        std::lock_guard lock(_mutex);
        if (_outcome.has_value())
            return false;
        _outcome = RenderOutcome{ _identity, std::move(owned), {} };
        _condition.notify_all();
        return true;
    }

    LazyRenderService::LazyRenderService(std::shared_ptr<IRenderServiceFactory> factory)
        : _factory(std::move(factory))
        , _ownerThread(std::this_thread::get_id())
    {
    }

    LazyRenderService::~LazyRenderService()
    {
        Shutdown();
    }

    IRenderService& LazyRenderService::Get()
    {
        if (std::this_thread::get_id() != _ownerThread)
            throw RenderServiceException(
                { RenderErrorCode::wrongThread, "Render service acquisition requires the context thread" });
        std::shared_ptr<IRenderServiceFactory> factory;
        {
            const std::lock_guard lock(_notificationMutex);
            if (_shutdown)
                throw RenderServiceException({ RenderErrorCode::shuttingDown, "Render service has shut down" });
            factory = _factory;
        }
        // Factories may re-enter context code; do not hold the notification lock across callbacks.
        if (factory && !factory->IsEnabled())
            throw RenderServiceException({ RenderErrorCode::unavailable, "The configured render service is not enabled" });
        {
            const std::lock_guard lock(_notificationMutex);
            if (_service)
                return *_service;
        }
        if (_failure)
            std::rethrow_exception(_failure);
        if (_creating)
            throw RenderServiceException({ RenderErrorCode::invalidState, "Recursive render service creation" });
        if (!factory)
            throw RenderServiceException(
                { RenderErrorCode::unavailable, "No render service was supplied for image production" });
        _creating = true;
        try
        {
            auto service = factory->Create();
            if (!service)
                throw RenderServiceException({ RenderErrorCode::creationFailed, "Render service factory returned no service" });
            {
                const std::lock_guard lock(_notificationMutex);
                if (!_shutdown)
                    _service = std::move(service);
            }
            if (service)
            {
                service->Shutdown();
                throw RenderServiceException({ RenderErrorCode::shuttingDown, "Render service shut down during creation" });
            }
        }
        catch (const RenderServiceException&)
        {
            _failure = std::current_exception();
        }
        catch (const std::exception& error)
        {
            _failure = std::make_exception_ptr(RenderServiceException({ RenderErrorCode::creationFailed, error.what() }));
        }
        catch (...)
        {
            _failure = std::make_exception_ptr(
                RenderServiceException({ RenderErrorCode::creationFailed, "Unknown render service initialization failure" }));
        }
        _creating = false;
        if (_failure)
            std::rethrow_exception(_failure);
        return *_service;
    }

    bool LazyRenderService::IsCreated() const noexcept
    {
        const std::lock_guard lock(_notificationMutex);
        return _service != nullptr;
    }

    void LazyRenderService::InvalidateImage(uint32_t image)
    {
        // Paint preparation workers may publish scrolling-text pixels. Serialise notification against service
        // publication/removal without invoking a factory or touching recording/GPU state on those workers.
        const std::lock_guard lock(_notificationMutex);
        // A service created later reads the current assets. Never instantiate one for asset loading or teardown.
        if (_service && !_shutdown)
            _service->InvalidateImage(image);
    }

    void LazyRenderService::Shutdown() noexcept
    {
        std::unique_ptr<IRenderService> service;
        std::shared_ptr<IRenderServiceFactory> factory;
        {
            const std::lock_guard lock(_notificationMutex);
            if (_shutdown)
                return;
            _shutdown = true;
            service = std::move(_service);
            factory = std::move(_factory);
        }
        // Existing notifications have returned, and new ones cannot reach the detached service. Shutdown and
        // object destruction run outside the lock so their cleanup may safely notify the context again.
        if (service)
            service->Shutdown();
    }
} // namespace OpenRCT2::Drawing
