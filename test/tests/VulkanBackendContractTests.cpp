/*****************************************************************************
 * Copyright (c) 2014-2026 OpenRCT2 developers
 *
 * For a complete list of all authors, please refer to contributors.md
 * Interested in contributing? Visit https://github.com/OpenRCT2/OpenRCT2
 *
 * OpenRCT2 is licensed under the GNU General Public License version 3.
 *****************************************************************************/

#include <gtest/gtest.h>

#ifdef ENABLE_VULKAN

    #include <chrono>
    #include <cstddef>
    #include <cstdint>
    #include <exception>
    #include <memory>
    #include <openrct2-renderer/vulkan/VulkanBackend.h>
    #include <openrct2-ui/drawing/engines/vulkan/VulkanDiagnosticCapture.h>
    #include <stdexcept>
    #include <string>
    #include <utility>
    #include <vector>

namespace
{
    namespace Gpu = OpenRCT2::Ui::Gpu;
    namespace Vulkan = OpenRCT2::Ui::Vulkan;

    struct PresentationHostCounters
    {
        uint32_t load = 0;
        uint32_t unload = 0;
        uint32_t extensionQueries = 0;
        uint32_t destroyed = 0;
        bool failLoad = false;
    };

    class FailingPresentationHost final : public Vulkan::PresentationHost
    {
    private:
        PresentationHostCounters& _counters;

    public:
        explicit FailingPresentationHost(PresentationHostCounters& counters)
            : _counters(counters)
        {
        }

        ~FailingPresentationHost() override
        {
            _counters.destroyed++;
        }

        std::unique_ptr<Vulkan::VulkanLibraryLease> AcquireVulkanLibrary() override
        {
            class TestLibraryLease final : public Vulkan::VulkanLibraryLease
            {
                PresentationHostCounters& _counters;

            public:
                explicit TestLibraryLease(PresentationHostCounters& counters)
                    : _counters(counters)
                {
                }
                ~TestLibraryLease() override
                {
                    _counters.unload++;
                }
            };
            _counters.load++;
            if (_counters.failLoad)
                throw std::runtime_error("injected loader failure");
            return std::make_unique<TestLibraryLease>(_counters);
        }

        std::vector<std::string> GetInstanceExtensions() override
        {
            _counters.extensionQueries++;
            // Device asks for host extensions before its first Vulkan call.
            throw std::runtime_error("injected extension query failure");
        }

        VkSurfaceKHR CreateSurface(VkInstance) override
        {
            ADD_FAILURE() << "The injected failure must precede instance/surface creation";
            throw std::logic_error("unexpected surface creation");
        }

        void DestroySurface(VkInstance, VkSurfaceKHR) noexcept override
        {
            ADD_FAILURE() << "No surface exists on this failure path";
        }

        VkExtent2D GetDrawableExtent() const noexcept override
        {
            ADD_FAILURE() << "Device must use the caller's captured drawable extent";
            return {};
        }
    };
} // namespace

TEST(VulkanPresentationHostTest, LoaderLeaseDoesNotRetainPresentationHost)
{
    PresentationHostCounters counters;
    std::unique_ptr<Vulkan::VulkanLibraryLease> lease;
    {
        FailingPresentationHost host(counters);
        lease = host.AcquireVulkanLibrary();
    }
    EXPECT_EQ(counters.destroyed, 1u);
    EXPECT_EQ(counters.load, 1u);
    EXPECT_EQ(counters.unload, 0u);
    lease.reset();
    EXPECT_EQ(counters.unload, 1u);
}

TEST(VulkanPresentationHostTest, ConstructionIsLazyAndBackendOwnsHostUntilDestruction)
{
    PresentationHostCounters counters;
    {
        auto backend = Vulkan::CreateBackend(std::make_unique<FailingPresentationHost>(counters));
        EXPECT_EQ(counters.load, 0u);
        EXPECT_EQ(counters.extensionQueries, 0u);
        backend->Dispose();
        backend->Dispose();
        EXPECT_EQ(counters.destroyed, 0u);
        EXPECT_EQ(counters.unload, 0u);
    }
    EXPECT_EQ(counters.destroyed, 1u);
    EXPECT_EQ(counters.load, 0u);
    EXPECT_EQ(counters.unload, 0u);
    EXPECT_THROW(static_cast<void>(Vulkan::CreateBackend(nullptr)), std::invalid_argument);
}

TEST(VulkanPresentationHostTest, FailedInitialisationBalancesOnlyAcquiredLoaderLeases)
{
    for (const bool failLoad : { false, true })
    {
        PresentationHostCounters counters;
        counters.failLoad = failLoad;
        {
            auto backend = Vulkan::CreateBackend(std::make_unique<FailingPresentationHost>(counters));
            Gpu::BackendConfig config{ .logicalExtent = { 1, 1 },
                                       .drawableExtent = { 1, 1 },
                                       .shaderDirectory = "unused-before-instance-creation" };
            for (uint32_t attempt = 1; attempt <= 2; attempt++)
            {
                EXPECT_THROW(backend->Initialise(config), std::runtime_error);
                EXPECT_EQ(counters.load, attempt);
                EXPECT_EQ(counters.extensionQueries, failLoad ? 0u : attempt);
                EXPECT_EQ(counters.unload, failLoad ? 0u : attempt);
                backend->Dispose();
                backend->Dispose();
                EXPECT_EQ(counters.unload, failLoad ? 0u : attempt);
                EXPECT_EQ(counters.destroyed, 0u);
            }
        }
        EXPECT_EQ(counters.destroyed, 1u);
        EXPECT_EQ(counters.unload, failLoad ? 0u : 2u);
    }
}

TEST(VulkanPresentationHostTest, SharedOwnerDefersBootstrapAndPreservesPresentationPolicy)
{
    PresentationHostCounters counters;
    FailingPresentationHost host(counters);
    Vulkan::DeviceContextOwner windowed(true);
    EXPECT_FALSE(windowed.IsCreated());
    EXPECT_THROW(windowed.AcquireOffscreen(), std::runtime_error);
    EXPECT_FALSE(windowed.IsCreated());
    EXPECT_EQ(counters.load, 0u);
    VkSurfaceKHR surface = VK_NULL_HANDLE;
    EXPECT_THROW(windowed.AcquireWindowed(host, surface), std::runtime_error);
    EXPECT_EQ(surface, VK_NULL_HANDLE);
    EXPECT_FALSE(windowed.IsCreated());
    EXPECT_EQ(counters.load, 1u);
    EXPECT_EQ(counters.unload, 1u);
    Vulkan::DeviceContextOwner graphicsOnly(false);
    EXPECT_THROW(graphicsOnly.AcquireWindowed(host, surface), std::logic_error);
    EXPECT_FALSE(graphicsOnly.IsCreated());
    EXPECT_EQ(counters.load, 1u);
}

TEST(VulkanDiagnosticCaptureTest, NamedFrameRejectsMismatchedResult)
{
    namespace Diagnostic = Vulkan::Diagnostic;
    Diagnostic::CaptureRequest request("named-frame");
    request.BindFrame(41);
    Diagnostic::CaptureResult stale;
    stale.output.frameNumber = 40;
    stale.coverage.frameNumber = 40;
    request.Complete(std::move(stale));
    EXPECT_THROW(static_cast<void>(request.Wait(std::chrono::milliseconds{ 0 })), std::runtime_error);
    EXPECT_FALSE(request.IsPending());
}

TEST(VulkanDiagnosticCaptureTest, TimeoutAndCancellationCannotBeOverwrittenByLateCompletion)
{
    namespace Diagnostic = Vulkan::Diagnostic;
    for (const bool timeout : { false, true })
    {
        Diagnostic::CaptureRequest request(timeout ? "timeout" : "cancelled");
        request.BindFrame(42);
        if (timeout)
        {
            EXPECT_THROW(static_cast<void>(request.Wait(std::chrono::milliseconds{ 0 })), std::runtime_error);
        }
        else
        {
            request.Fail(std::make_exception_ptr(std::runtime_error("superseded")));
        }
        Diagnostic::CaptureResult late;
        late.output.frameNumber = 42;
        late.coverage.frameNumber = 42;
        request.Complete(std::move(late));
        EXPECT_THROW(static_cast<void>(request.Wait(std::chrono::milliseconds{ 0 })), std::runtime_error);
        EXPECT_FALSE(request.IsPending());
    }
}

TEST(VulkanDiagnosticCaptureTest, SuccessfulResultOwnsNamedBuffersAndCanBeConsumedOnlyOnce)
{
    namespace Diagnostic = Vulkan::Diagnostic;
    Diagnostic::CaptureRequest request("owned-frame");
    std::array<uint64_t, 4> preparation{17, 2, 3, 41};
    request.BindFrame(0, 9, preparation);
    preparation = {99, 99, 99, 99}; // A later owner update cannot change the named packet.
    EXPECT_THROW(request.BindFrame(1), std::logic_error);
    Diagnostic::CaptureResult result;
    result.terrainPreparation = {88, 88, 88, 88}; // Worker data cannot replace owner-latched counters.
    result.output.frameNumber = 0;
    result.coverage.frameNumber = 0;
    result.indexed = { std::byte{ 7 } };
    result.output.rgba = { std::byte{ 1 }, std::byte{ 2 }, std::byte{ 3 }, std::byte{ 255 } };
    request.Complete(std::move(result));
    request.Fail(std::make_exception_ptr(std::runtime_error("late shutdown")));
    const auto captured = request.Wait(std::chrono::milliseconds{ 0 });
    EXPECT_EQ(captured.name, "owned-frame");
    EXPECT_EQ(captured.terrainPreparation, (std::array<uint64_t, 4>{17, 2, 3, 41}));
    EXPECT_EQ(captured.atlasLease, 9u);
    ASSERT_EQ(captured.indexed.size(), 1u);
    EXPECT_EQ(captured.indexed[0], std::byte{ 7 });
    ASSERT_EQ(captured.output.rgba.size(), 4u);
    EXPECT_EQ(captured.output.rgba[3], std::byte{ 255 });
    EXPECT_THROW(static_cast<void>(request.Wait(std::chrono::milliseconds{ 0 })), std::logic_error);
    EXPECT_THROW(request.BindFrame(2), std::logic_error);
}

TEST(VulkanDiagnosticCaptureTest, BalloonPublicationIsOwnedByNamedRequest)
{
    using namespace OpenRCT2::Ui::Vulkan::Diagnostic;
    CaptureRequest request("balloon-owned");
    BalloonPublication publication;
    publication.retained = true;
    publication.epoch = 3;
    publication.sequence = 8;
    publication.count = 1;
    publication.records.push_back({ 7, 10, 20, 30, 1, 0, 2, 4, 13, 22, 11, 0 });
    request.BindBalloonPublication(publication);
    publication.records.front()[1] = 999;
    EXPECT_THROW(request.BindBalloonPublication(publication), std::logic_error);
    request.BindFrame(18);
    CaptureResult result;
    result.output.frameNumber = result.coverage.frameNumber = 18;
    request.Complete(std::move(result));
    const auto captured = request.Wait(std::chrono::milliseconds(1));
    ASSERT_TRUE(captured.balloonPublication.has_value());
    EXPECT_EQ(captured.balloonPublication->records.front()[1], 10);
    EXPECT_EQ(captured.balloonPublication->sequence, 8u);
    EXPECT_THROW(request.BindBalloonPublication({}), std::logic_error);
}

TEST(VulkanDiagnosticCaptureTest, BalloonPublicationCannotBindAfterSealOrCancellation)
{
    using namespace OpenRCT2::Ui::Vulkan::Diagnostic;
    CaptureRequest sealed("sealed");
    sealed.BindFrame(1);
    EXPECT_THROW(sealed.BindBalloonPublication({}), std::logic_error);
    CaptureRequest cancelled("cancelled");
    cancelled.BindBalloonPublication({});
    cancelled.Fail(std::make_exception_ptr(std::runtime_error("cancelled")));
    EXPECT_THROW(cancelled.BindBalloonPublication({}), std::logic_error);
    EXPECT_THROW(static_cast<void>(cancelled.Wait(std::chrono::milliseconds(1))), std::runtime_error);
}

#endif // ENABLE_VULKAN
