/*****************************************************************************
 * Copyright (c) 2014-2026 OpenRCT2 developers
 *
 * For a complete list of all authors, please refer to contributors.md
 * Interested in contributing? Visit https://github.com/OpenRCT2/OpenRCT2
 *
 * OpenRCT2 is licensed under the GNU General Public License version 3.
 *****************************************************************************/

#pragma once

#include "GpuBackend.h"
#include "GpuTextureCache.h"

#include <array>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <limits>
#include <memory>
#include <mutex>
#include <span>
#include <stdexcept>
#include <utility>
#include <vector>

namespace OpenRCT2::Ui::Gpu
{
    template<typename T>
    class SynchronousResult
    {
    protected:
        void Complete(T result)
        {
            {
                std::scoped_lock lock(_mutex);
                if (_complete)
                {
                    return;
                }
                _result = std::move(result);
                _complete = true;
            }
            _completed.notify_all();
        }

        void Fail(std::exception_ptr error)
        {
            {
                std::scoped_lock lock(_mutex);
                if (_complete)
                {
                    return;
                }
                _error = std::move(error);
                _complete = true;
            }
            _completed.notify_all();
        }

        [[nodiscard]] T Wait()
        {
            std::unique_lock lock(_mutex);
            _completed.wait(lock, [this] { return _complete; });
            if (_error)
            {
                std::rethrow_exception(_error);
            }
            return std::move(_result);
        }

    private:
        std::mutex _mutex;
        std::condition_variable _completed;
        std::exception_ptr _error;
        T _result{};
        bool _complete = false;
    };

    class SynchronousFrameBoundary final : private SynchronousResult<bool>
    {
    public:
        using SynchronousResult::Fail;

        void Complete()
        {
            SynchronousResult::Complete(true);
        }

        void Wait()
        {
            static_cast<void>(SynchronousResult::Wait());
        }
    };

    class SynchronousReadback final : private SynchronousResult<bool>
    {
    public:
        using SynchronousResult::Complete;
        using SynchronousResult::Fail;
        using SynchronousResult::Wait;

        explicit SynchronousReadback(Extent extent)
            : _extent(extent)
        {
            if (extent.width == 0 || extent.height == 0
                || extent.width > std::numeric_limits<size_t>::max() / extent.height)
            {
                throw std::invalid_argument("Invalid synchronous GPU readback extent");
            }
            _pixels.resize(static_cast<size_t>(extent.width) * extent.height);
        }

        [[nodiscard]] Extent GetExtent() const noexcept
        {
            return _extent;
        }

        [[nodiscard]] std::span<std::byte> GetPixels() noexcept
        {
            return _pixels;
        }

    private:
        Extent _extent{};
        std::vector<std::byte> _pixels;
    };

    struct FramePresentationSnapshot
    {
        Extent logicalExtent{};
        Extent drawableExtent{};
        PresentMode presentMode = PresentMode::VSync;
        std::array<std::byte, 256 * 4> palette{};
        uint64_t resizeVersion = 0;
        uint64_t surfaceFormatVersion = 0;
        uint64_t presentModeVersion = 0;
        uint64_t paletteVersion = 0;
        uint64_t graphicsLookupTablesVersion = 0;
        bool hasPalette = false;
    };

    struct RecordedFramePacket
    {
        uint64_t frameNumber = 0;
        FrameCommandStream commands;
        AtlasResidencyToken residency;
        FramePresentationSnapshot presentation;
        std::shared_ptr<SynchronousReadback> readback;
        std::shared_ptr<SynchronousFrameBoundary> timingBoundary;
        bool hasVisualFrame = false;
    };

    /**
     * One pending newest frame plus one returned packet for allocation reuse.
     * Publishing replaces stale visual work instead of queuing simulation
     * history behind the renderer.
     */
    class LatestFrameMailbox final
    {
    public:
        struct PublishResult
        {
            bool accepted = false;
            std::unique_ptr<RecordedFramePacket> released;
        };

        template<typename Request>
        struct ControlPublishResult
        {
            bool accepted = false;
            std::shared_ptr<Request> released;
        };

        using ReadbackPublishResult = ControlPublishResult<SynchronousReadback>;
        using BoundaryPublishResult = ControlPublishResult<SynchronousFrameBoundary>;

        [[nodiscard]] PublishResult Publish(std::unique_ptr<RecordedFramePacket> packet)
        {
            if (packet == nullptr)
            {
                throw std::invalid_argument("Cannot publish a null GPU frame packet");
            }

            PublishResult result;
            {
                std::scoped_lock lock(_mutex);
                if (_stopping)
                {
                    result.released = std::move(packet);
                    return result;
                }
                if (_newest != nullptr && _newest->readback != nullptr && packet->readback == nullptr)
                {
                    // A synchronous capture follows the newest disposable
                    // visual frame instead of being discarded with the
                    // superseded packet it was first attached to.
                    packet->readback = std::move(_newest->readback);
                }
                if (_newest != nullptr && _newest->timingBoundary != nullptr && packet->timingBoundary == nullptr)
                {
                    packet->timingBoundary = std::move(_newest->timingBoundary);
                }
                result.accepted = true;
                result.released = std::exchange(_newest, std::move(packet));
            }
            _available.notify_one();
            return result;
        }

        /**
         * Attaches an explicit readback to the newest visual packet when one
         * is pending. Otherwise a control-only packet is published. This
         * preserves visual-before-capture ordering without growing an
         * unbounded presentation queue.
         */
        [[nodiscard]] ReadbackPublishResult PublishReadback(std::shared_ptr<SynchronousReadback> readback)
        {
            return PublishControl(
                std::move(readback), &RecordedFramePacket::readback,
                "Cannot publish a null GPU readback request");
        }

        /**
         * Attaches a benchmark-only completion boundary to the newest visual
         * packet, or publishes a control packet when no visual work is queued.
         */
        [[nodiscard]] BoundaryPublishResult PublishTimingBoundary(
            std::shared_ptr<SynchronousFrameBoundary> boundary)
        {
            return PublishControl(
                std::move(boundary), &RecordedFramePacket::timingBoundary,
                "Cannot publish a null GPU timing boundary");
        }

        [[nodiscard]] std::unique_ptr<RecordedFramePacket> WaitTakeNewest()
        {
            std::unique_lock lock(_mutex);
            _available.wait(lock, [this] { return _stopping || _newest != nullptr; });
            return std::move(_newest);
        }

        [[nodiscard]] std::unique_ptr<RecordedFramePacket> Stop()
        {
            std::unique_ptr<RecordedFramePacket> pending;
            {
                std::scoped_lock lock(_mutex);
                _stopping = true;
                pending = std::move(_newest);
            }
            _available.notify_all();
            return pending;
        }

        void Recycle(std::unique_ptr<RecordedFramePacket> packet)
        {
            if (packet == nullptr)
            {
                return;
            }
            std::scoped_lock lock(_mutex);
            if (!_stopping)
            {
                _recycled = std::move(packet);
            }
        }

        [[nodiscard]] std::unique_ptr<RecordedFramePacket> TakeRecycled()
        {
            std::scoped_lock lock(_mutex);
            return std::move(_recycled);
        }

    private:
        template<typename Request>
        [[nodiscard]] ControlPublishResult<Request> PublishControl(
            std::shared_ptr<Request> request, std::shared_ptr<Request> RecordedFramePacket::* destination,
            const char* nullError)
        {
            if (request == nullptr)
            {
                throw std::invalid_argument(nullError);
            }

            ControlPublishResult<Request> result;
            {
                std::scoped_lock lock(_mutex);
                if (_stopping)
                {
                    result.released = std::move(request);
                    return result;
                }
                if (_newest == nullptr)
                {
                    _newest = std::make_unique<RecordedFramePacket>();
                }
                result.accepted = true;
                result.released = std::exchange(_newest.get()->*destination, std::move(request));
            }
            _available.notify_one();
            return result;
        }

        mutable std::mutex _mutex;
        std::condition_variable _available;
        std::unique_ptr<RecordedFramePacket> _newest;
        std::unique_ptr<RecordedFramePacket> _recycled;
        bool _stopping = false;
    };
} // namespace OpenRCT2::Ui::Gpu
