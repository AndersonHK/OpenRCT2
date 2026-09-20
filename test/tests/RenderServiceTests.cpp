/*****************************************************************************
 * Copyright (c) 2014-2026 OpenRCT2 developers
 * OpenRCT2 is licensed under the GNU General Public License version 3.
 *****************************************************************************/

#include <functional>
#include <future>
#include <gtest/gtest.h>
#include <openrct2-renderer/RenderServiceFactory.h>
#include <openrct2/OpenRCT2.h>
#include <openrct2/config/Config.h>
#include <openrct2/drawing/IDrawingEngine.h>
#include <openrct2/drawing/RenderService.h>

using namespace OpenRCT2::Drawing;
using namespace std::chrono_literals;

namespace
{
    OffscreenRenderRequest Request()
    {
        OffscreenRenderRequest request;
        request.name = "preview-rotation-2";
        request.logicalExtent = { 2, 1 };
        request.outputExtent = { 4, 2 };
        request.rgbaOutput = true;
        return request;
    }

    RenderSubmissionIdentity Identity()
    {
        return { 7, 3, 11, "preview-rotation-2" };
    }

    RenderResult Result()
    {
        RenderResult result;
        result.identity = Identity();
        result.logicalExtent = Request().logicalExtent;
        result.outputExtent = Request().outputExtent;
        result.indexed = { std::byte{ 9 }, std::byte{ 42 } };
        result.rgba.resize(32, std::byte{ 67 });
        return result;
    }

    struct ServiceCounts
    {
        int created{};
        int shutDown{};
        int destroyed{};
    };

    class TestService final : public IRenderService
    {
    public:
        explicit TestService(ServiceCounts& counts)
            : _counts(counts)
        {
        }
        ~TestService() override
        {
            _counts.destroyed++;
        }
        std::unique_ptr<IRenderSession> BeginOffscreen(OffscreenRenderRequest) override
        {
            throw RenderServiceException({ RenderErrorCode::executionFailed, "This factory test does not rasterize" });
        }
        void Shutdown() noexcept override
        {
            _counts.shutDown++;
        }

    private:
        ServiceCounts& _counts;
    };

    class TestFactory final : public IRenderServiceFactory
    {
    public:
        explicit TestFactory(ServiceCounts& counts)
            : _counts(counts)
        {
        }
        bool fail{};
        bool enabled{ true };
        bool IsEnabled() const override { return enabled; }
        bool returnNull{};
        std::function<void()> onCreate;
        std::unique_ptr<IRenderService> Create() override
        {
            _counts.created++;
            if (onCreate)
                onCreate();
            if (fail)
                throw std::runtime_error("loader unavailable");
            if (returnNull)
                return {};
            return std::make_unique<TestService>(_counts);
        }

    private:
        ServiceCounts& _counts;
    };

    template<typename F>
    void ExpectServiceError(F&& operation, RenderErrorCode expected)
    {
        try
        {
            operation();
            FAIL() << "Expected typed render service error";
        }
        catch (const RenderServiceException& error)
        {
            EXPECT_EQ(error.GetCode(), expected);
        }
    }
} // namespace

TEST(RenderServiceContract, RejectsInvalidInitialContentsAndOverflowWithoutAllocating)
{
    auto request = Request();
    request.initialContents = RenderInitialContents::ownedIndices;
    EXPECT_THROW(ValidateOffscreenRenderRequest(request), RenderServiceException);
    request.initialIndices = { std::byte{ 0 }, std::byte{ 255 } };
    EXPECT_NO_THROW(ValidateOffscreenRenderRequest(request));
    request.initialContents = RenderInitialContents::clearIndex;
    EXPECT_THROW(ValidateOffscreenRenderRequest(request), RenderServiceException);
    request.initialIndices.clear();
    request.logicalExtent.width = UINT32_MAX;
    EXPECT_THROW(ValidateOffscreenRenderRequest(request), RenderServiceException);
}

TEST(RenderServiceContract, CompletedBytesSurviveProducerAndCompletionLifetime)
{
    std::shared_ptr<const RenderResult> image;
    {
        RenderCompletion completion(Identity(), Request());
        auto source = Result();
        EXPECT_TRUE(completion.Complete(std::move(source)));
        source.indexed = { std::byte{ 0 } };
        image = completion.Wait(0ms).result;
        ASSERT_NE(image, nullptr);
        EXPECT_FALSE(completion.Fail({ RenderErrorCode::deviceLost, "late device loss" }));
        completion.Cancel();
        EXPECT_EQ(completion.Wait(0ms).result, image);
    }
    EXPECT_EQ(image->identity, Identity());
    EXPECT_EQ(image->indexed, Result().indexed);
    EXPECT_EQ(image->rgba, Result().rgba);
}

TEST(RenderServiceContract, ReusedTargetGenerationCannotSatisfyOlderSubmission)
{
    RenderCompletion completion(Identity(), Request());
    auto stale = Result();
    stale.identity.targetGeneration++;
    EXPECT_TRUE(completion.Complete(std::move(stale)));
    const auto outcome = completion.Wait(0ms);
    ASSERT_TRUE(outcome.error);
    EXPECT_EQ(outcome.identity, Identity());
    EXPECT_EQ(outcome.error->code, RenderErrorCode::staleTarget);
    EXPECT_EQ(outcome.result, nullptr);
    EXPECT_FALSE(completion.Complete(Result()));
}

TEST(RenderServiceContract, InvalidByteCountFailsInsteadOfReturningPartialImage)
{
    RenderCompletion completion(Identity(), Request());
    auto result = Result();
    result.rgba.pop_back();
    completion.Complete(std::move(result));
    auto outcome = completion.Wait(0ms);
    ASSERT_TRUE(outcome.error);
    EXPECT_EQ(outcome.error->code, RenderErrorCode::executionFailed);
    EXPECT_EQ(outcome.result, nullptr);
}

TEST(RenderServiceContract, TimeoutDoesNotConsumeCompletionAndWaitingReaderWakes)
{
    auto completion = std::make_shared<RenderCompletion>(Identity(), Request());
    const auto timeout = completion->Wait(0ms);
    ASSERT_TRUE(timeout.error);
    EXPECT_EQ(timeout.error->code, RenderErrorCode::timeout);
    auto reader = std::async(std::launch::async, [completion]() { return completion->Wait(5s); });
    EXPECT_TRUE(completion->Complete(Result()));
    const auto outcome = reader.get();
    EXPECT_FALSE(outcome.error);
    ASSERT_NE(outcome.result, nullptr);
    EXPECT_EQ(outcome.result->indexed, Result().indexed);
}

TEST(RenderServiceContract, CancellationIsTerminalButDoesNotDestroySubmissionResources)
{
    auto completion = std::make_shared<RenderCompletion>(Identity(), Request());
    auto jobLease = std::make_shared<int>(42);
    std::weak_ptr<int> observedLease = jobLease;
    // A submitted job independently retains its fence/resource domain until GPU completion, even without a waiting client.
    std::function<bool()> submittedJob = [completion, retained = std::move(jobLease)]() {
        EXPECT_EQ(*retained, 42);
        return completion->Complete(Result());
    };
    completion->Cancel();
    EXPECT_EQ(completion->Wait(0ms).error->code, RenderErrorCode::cancelled);
    completion.reset();
    EXPECT_FALSE(observedLease.expired());
    EXPECT_FALSE(submittedJob());
    submittedJob = {};
    EXPECT_TRUE(observedLease.expired());
}

TEST(RenderServiceContract, DeviceLossAndShutdownWakeNamedWaitersWithoutImage)
{
    for (const auto code : { RenderErrorCode::deviceLost, RenderErrorCode::shuttingDown })
    {
        auto completion = std::make_shared<RenderCompletion>(Identity(), Request());
        auto reader = std::async(std::launch::async, [completion]() { return completion->Wait(5s); });
        EXPECT_TRUE(completion->Fail({ code, "submission failed" }));
        const auto outcome = reader.get();
        EXPECT_EQ(outcome.identity, Identity());
        ASSERT_TRUE(outcome.error);
        EXPECT_EQ(outcome.error->code, code);
        EXPECT_EQ(outcome.result, nullptr);
    }
}

TEST(RenderServiceContract, SubmissionWorkerCannotBlockOnItsOwnPendingResult)
{
    RenderCompletion completion(Identity(), Request(), std::this_thread::get_id());
    auto outcome = completion.Wait(5s);
    ASSERT_TRUE(outcome.error);
    EXPECT_EQ(outcome.error->code, RenderErrorCode::wrongThread);
    completion.Complete(Result());
    EXPECT_NE(completion.Wait(0ms).result, nullptr);
}

TEST(RenderServiceLazyLifetime, UnusedFactoryIsNeverInvokedIncludingShutdown)
{
    ServiceCounts counts;
    {
        LazyRenderService service(std::make_shared<TestFactory>(counts));
        EXPECT_FALSE(service.IsCreated());
        service.Shutdown();
        service.Shutdown();
    }
    EXPECT_EQ(counts.created, 0);
    EXPECT_EQ(counts.shutDown, 0);
    EXPECT_EQ(counts.destroyed, 0);
}

TEST(RenderServiceLazyLifetime, DisabledSelectionDoesNotCreateOrPoisonLaterEnabledService)
{
    ServiceCounts counts;
    auto factory = std::make_shared<TestFactory>(counts);
    LazyRenderService service(factory);
    factory->enabled = false;
    ExpectServiceError([&]() { service.Get(); }, RenderErrorCode::unavailable);
    EXPECT_EQ(counts.created, 0);
    factory->enabled = true;
    auto* first = &service.Get();
    factory->enabled = false;
    ExpectServiceError([&]() { service.Get(); }, RenderErrorCode::unavailable);
    factory->enabled = true;
    EXPECT_EQ(&service.Get(), first);
    EXPECT_EQ(counts.created, 1);
    service.Shutdown();
    EXPECT_EQ(counts.shutDown, 1);
}

TEST(RenderServiceLazyLifetime, ProductionSelectionReadsCurrentConfigurationWithoutCreatingGraphics)
{
    struct RestoreConfiguration
    {
        DrawingEngine saved = OpenRCT2::Config::Get().general.drawingEngine;
        std::optional<DrawingEngine> overrideSaved = gIntegratedBenchmark.drawingEngine;
        ~RestoreConfiguration()
        {
            OpenRCT2::Config::Get().general.drawingEngine = saved;
            gIntegratedBenchmark.drawingEngine = overrideSaved;
        }
    } restore;
    gIntegratedBenchmark.drawingEngine.reset();
    auto factory = OpenRCT2::Renderer::CreateConfiguredRenderServiceFactory();
    LazyRenderService service(factory);
    OpenRCT2::Config::Get().general.drawingEngine = DrawingEngine::softwareWithHardwareDisplay;
    EXPECT_FALSE(factory->IsEnabled());
    ExpectServiceError([&]() { service.Get(); }, RenderErrorCode::unavailable);
    EXPECT_FALSE(service.IsCreated());
    OpenRCT2::Config::Get().general.drawingEngine = DrawingEngine::vulkan;
    EXPECT_TRUE(factory->IsEnabled());
    EXPECT_FALSE(service.IsCreated());
    OpenRCT2::Config::Get().general.drawingEngine = DrawingEngine::softwareWithHardwareDisplay;
    EXPECT_FALSE(factory->IsEnabled());
    gIntegratedBenchmark.drawingEngine = DrawingEngine::vulkan;
    EXPECT_TRUE(factory->IsEnabled());
    gIntegratedBenchmark.drawingEngine.reset();
    service.Shutdown();
}

TEST(RenderServiceLazyLifetime, CreatesOnceAndShutsDownBeforeDestruction)
{
    ServiceCounts counts;
    {
        LazyRenderService service(std::make_shared<TestFactory>(counts));
        EXPECT_EQ(&service.Get(), &service.Get());
        EXPECT_TRUE(service.IsCreated());
        EXPECT_EQ(counts.created, 1);
        service.Shutdown();
        EXPECT_EQ(counts.shutDown, 1);
        EXPECT_EQ(counts.destroyed, 1);
        ExpectServiceError([&]() { service.Get(); }, RenderErrorCode::shuttingDown);
    }
    EXPECT_EQ(counts.shutDown, 1);
}

TEST(RenderServiceLazyLifetime, MissingOrFailedFactoryHasExplicitImageOperationError)
{
    LazyRenderService absent;
    ExpectServiceError([&]() { absent.Get(); }, RenderErrorCode::unavailable);
    for (const bool returnNull : { false, true })
    {
        ServiceCounts counts;
        auto factory = std::make_shared<TestFactory>(counts);
        factory->returnNull = returnNull;
        factory->fail = !returnNull;
        LazyRenderService service(factory);
        ExpectServiceError([&]() { service.Get(); }, RenderErrorCode::creationFailed);
        ExpectServiceError([&]() { service.Get(); }, RenderErrorCode::creationFailed);
        EXPECT_EQ(counts.created, 1);
        EXPECT_FALSE(service.IsCreated());
    }
}

TEST(RenderServiceLazyLifetime, WrongThreadAndRecursiveAcquisitionDoNotInitializeAnotherService)
{
    ServiceCounts counts;
    auto factory = std::make_shared<TestFactory>(counts);
    LazyRenderService service(factory);
    auto reader = std::async(
        std::launch::async, [&]() { ExpectServiceError([&]() { service.Get(); }, RenderErrorCode::wrongThread); });
    reader.get();
    EXPECT_EQ(counts.created, 0);
    factory->onCreate = [&]() { service.Get(); };
    ExpectServiceError([&]() { service.Get(); }, RenderErrorCode::invalidState);
    ExpectServiceError([&]() { service.Get(); }, RenderErrorCode::invalidState);
    EXPECT_EQ(counts.created, 1);
}

TEST(RenderServiceLazyLifetime, ShutdownDuringFactoryCreationRetiresNewService)
{
    ServiceCounts counts;
    auto factory = std::make_shared<TestFactory>(counts);
    LazyRenderService service(factory);
    factory->onCreate = [&]() { service.Shutdown(); };
    ExpectServiceError([&]() { service.Get(); }, RenderErrorCode::shuttingDown);
    EXPECT_FALSE(service.IsCreated());
    EXPECT_EQ(counts.created, 1);
    EXPECT_EQ(counts.shutDown, 1);
    EXPECT_EQ(counts.destroyed, 1);
}
