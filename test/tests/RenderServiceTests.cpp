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
#include <openrct2/ui/UiContext.h>
#include <stdexcept>
#ifdef ENABLE_VULKAN
    #include <array>
    #include <cstdlib>
    #include <filesystem>
    #include <stdexcept>
    #include <string>
    #include <string_view>
    #include <vector>
    #include <openrct2-renderer/vulkan/VulkanDeviceContext.h>
    #include <openrct2/Context.h>
    #include <openrct2/GameState.h>
    #include <openrct2/PlatformEnvironment.h>
    #include <openrct2/SpriteIds.h>
    #include <openrct2/drawing/Drawing.Sprite.h>
    #include <openrct2/drawing/G1Element.h>
    #include <openrct2/drawing/IDrawingContext.h>
    #include <openrct2/drawing/PresentationGeneration.h>
    #include <openrct2/entity/EntityPresentationSnapshot.h>
    #include <openrct2/world/MapPresentationSnapshot.h>
    #ifdef ENABLE_SCRIPTING
        #include <openrct2/scripting/ScriptEngine.h>
    #endif
#endif

using namespace OpenRCT2::Drawing;
using namespace std::chrono_literals;

TEST(RenderServiceContract, NongraphicalUiDoesNotCreateImplicitDisplayRenderer)
{
    auto ui = OpenRCT2::Ui::CreateDummyUiContext();
    auto factory = ui->GetDrawingEngineFactory();
    ASSERT_NE(factory, nullptr);
    EXPECT_THROW(static_cast<void>(factory->Create(*ui)), std::runtime_error);
}

TEST(RenderServiceContract, OrderedBitmapAliasRejectsInvalidGeometryAndMixedOutputs)
{
    OffscreenRenderRequest request;
    request.name = "alias-validation";
    request.logicalExtent = request.outputExtent = { 4, 2 };
    request.initialContents = RenderInitialContents::ownedIndices;
    request.initialIndices.resize(8);
    request.orderedAlias = OrderedImageAlias{ .destinationX = 1, .width = 3, .height = 2 };
    ASSERT_NO_THROW(ValidateOffscreenRenderRequest(request));
    auto invalid = request;
    invalid.orderedAlias->destinationX = UINT32_MAX;
    EXPECT_THROW(ValidateOffscreenRenderRequest(invalid), RenderServiceException);
    invalid = request;
    invalid.orderedAlias->sourceY = 1;
    EXPECT_THROW(ValidateOffscreenRenderRequest(invalid), RenderServiceException);
    invalid = request;
    invalid.orderedAlias->width = 0;
    EXPECT_THROW(ValidateOffscreenRenderRequest(invalid), RenderServiceException);
    invalid = request;
    invalid.rgbaOutput = true;
    EXPECT_THROW(ValidateOffscreenRenderRequest(invalid), RenderServiceException);
    invalid = request;
    invalid.outputExtent.width = 8;
    EXPECT_THROW(ValidateOffscreenRenderRequest(invalid), RenderServiceException);
    invalid = request;
    invalid.initialIndices.pop_back();
    EXPECT_THROW(ValidateOffscreenRenderRequest(invalid), RenderServiceException);
}

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
        std::vector<uint32_t> invalidations;
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
        void InvalidateImage(uint32_t image) override
        {
            _counts.invalidations.push_back(image);
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

TEST(RenderServiceLazyLifetime, ProductionFactoryIsAlwaysEnabledAndRemainsLazy)
{
    auto factory = OpenRCT2::Renderer::CreateConfiguredRenderServiceFactory();
    LazyRenderService service(factory);
    EXPECT_TRUE(factory->IsEnabled());
    EXPECT_FALSE(service.IsCreated());
    service.Shutdown();
    EXPECT_FALSE(service.IsCreated());
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

TEST(RenderServiceLazyLifetime, ImageInvalidationOnlyReachesAnExistingService)
{
    ServiceCounts counts;
    LazyRenderService service(std::make_shared<TestFactory>(counts));
    service.InvalidateImage(12);
    EXPECT_FALSE(service.IsCreated());
    EXPECT_EQ(counts.created, 0);
    static_cast<void>(service.Get());
    service.InvalidateImage(34);
    service.InvalidateImage(56);
    EXPECT_EQ(counts.invalidations, (std::vector<uint32_t>{ 34, 56 }));
    service.Shutdown();
    service.InvalidateImage(78);
    EXPECT_EQ(counts.invalidations, (std::vector<uint32_t>{ 34, 56 }));
    EXPECT_EQ(counts.created, 1);
}

TEST(RenderServiceLazyLifetime, WorkerImageNotificationsDoNotCreateOrOutliveTheService)
{
    ServiceCounts counts;
    LazyRenderService service(std::make_shared<TestFactory>(counts));
    const auto notify = [&](uint32_t image) {
        std::exception_ptr failure;
        std::thread worker([&] {
            try { service.InvalidateImage(image); }
            catch (...) { failure = std::current_exception(); }
        });
        worker.join();
        EXPECT_EQ(failure, nullptr);
    };
    notify(12);
    EXPECT_FALSE(service.IsCreated());
    EXPECT_EQ(counts.created, 0);
    static_cast<void>(service.Get());
    notify(34);
    EXPECT_EQ(counts.invalidations, (std::vector<uint32_t>{ 34 }));
    service.Shutdown();
    notify(56);
    EXPECT_EQ(counts.invalidations, (std::vector<uint32_t>{ 34 }));
    EXPECT_EQ(counts.created, 1);
    EXPECT_EQ(counts.destroyed, 1);
}

#ifdef ENABLE_VULKAN
class RenderServiceProductionRecordingTest : public testing::Test
{
protected:
    bool oldHeadless = gOpenRCT2Headless;
    bool oldNoGraphics = gOpenRCT2NoGraphics;
    std::string oldRct1 = OpenRCT2::Config::Get().general.rct1Path;
    std::string oldRct2 = OpenRCT2::Config::Get().general.rct2Path;
    std::unique_ptr<OpenRCT2::IContext> context;
    bool contextReady{};

    void SetUp() override
    {
        using namespace OpenRCT2;
        const auto* rct2 = std::getenv("OPENRCT2_TEST_RCT2_PATH");
        if (rct2 == nullptr || *rct2 == '\0')
        {
            const auto* required = std::getenv("OPENRCT2_REQUIRE_VULKAN_TESTS");
            if (required && std::string_view(required) == "1")
                FAIL() << "Production recording requires pinned OPENRCT2_TEST_RCT2_PATH";
            GTEST_SKIP() << "Production recording requires OPENRCT2_TEST_RCT2_PATH";
        }
        gOpenRCT2Headless = true;
        gOpenRCT2NoGraphics = false;
        context = CreateContext();
        ASSERT_NE(context, nullptr);
        auto& environment = context->GetPlatformEnvironment();
        Config::Get().general.rct2Path = rct2;
        environment.SetBasePath(DirBase::rct2, rct2);
        if (const auto* rct1 = std::getenv("OPENRCT2_TEST_RCT1_PATH"))
        {
            Config::Get().general.rct1Path = rct1;
            environment.SetBasePath(DirBase::rct1, rct1);
        }
        auto data = std::filesystem::current_path() / "data";
        if (const auto* shaders = std::getenv("OPENRCT2_VULKAN_SHADER_DIRECTORY"))
            data = std::filesystem::path(shaders).parent_path().parent_path();
        else if (!std::filesystem::is_regular_file(data / "g2.dat"))
            data = std::filesystem::current_path() / "bin/data";
        environment.SetBasePath(DirBase::openrct2, data.string());
        ASSERT_TRUE(context->Initialise());
        contextReady = true;
    }
    void TearDown() override
    {
#ifdef ENABLE_SCRIPTING
        if (context && !contextReady)
        {
            try { context->GetScriptEngine().Initialise(); }
            catch (const std::runtime_error& error)
            {
                EXPECT_STREQ(error.what(), "Script engine already initialised.");
            }
        }
#endif
        context.reset();
        gOpenRCT2Headless = oldHeadless;
        gOpenRCT2NoGraphics = oldNoGraphics;
        OpenRCT2::Config::Get().general.rct1Path = oldRct1;
        OpenRCT2::Config::Get().general.rct2Path = oldRct2;
    }
};

TEST_F(RenderServiceProductionRecordingTest, RecordsFiveSpriteSizeClassesWithoutAcquiringDevice)
{
    using namespace OpenRCT2;
    struct RestoreSprites
    {
        std::array<G1Element, 5> saved{};
        RestoreSprites()
        {
            for (uint32_t i = 0; i < saved.size(); ++i)
                saved[i] = *GfxGetG1Element(SPR_TEMP_BEGIN + i);
        }
        ~RestoreSprites()
        {
            for (uint32_t i = 0; i < saved.size(); ++i)
                GfxSetG1Element(SPR_TEMP_BEGIN + i, &saved[i]);
        }
    } restore;
    std::array<std::vector<uint8_t>, 5> pixels;
    for (uint32_t i = 0; i < pixels.size(); ++i)
    {
        const auto width = static_cast<int16_t>(32 << i);
        pixels[i].resize(width, 42);
        const G1Element sprite{ .offset = pixels[i].data(), .width = width, .height = 1 };
        GfxSetG1Element(SPR_TEMP_BEGIN + i, &sprite);
    }
    auto owner = std::make_shared<Ui::Vulkan::DeviceContextOwner>(false);
    auto service = Renderer::CreateConfiguredRenderServiceFactory(owner)->Create();
    auto request = Request();
    request.logicalExtent = request.outputExtent = { 512, 8 };
    // Retry after cancellation proves the fifth class is representable without acquiring a GPU or losing the service.
    for (uint32_t recording = 0; recording < 2; ++recording)
    {
        auto session = service->BeginOffscreen(request);
        for (uint32_t i = 0; i < pixels.size(); ++i)
            ASSERT_NO_THROW(session->GetDrawingContext().DrawSprite(
                session->GetRenderTarget(), ImageId(SPR_TEMP_BEGIN + i), 0, static_cast<int32_t>(i)));
        EXPECT_FALSE(owner->IsCreated());
        session->Cancel();
    }
    service->Shutdown();
    EXPECT_FALSE(owner->IsCreated());
}
TEST_F(RenderServiceProductionRecordingTest, NativeWorldJobsRetainRevisionsAndRetireBeforeForegroundReuse)
{
    using namespace OpenRCT2;
    const auto image = SPR_TEMP_BEGIN;
    const auto original = *GfxGetG1Element(image);
    struct RestoreSprite
    {
        ImageIndex image;
        G1Element original;
        ~RestoreSprite() { GfxSetG1Element(image, &original); }
    } restore{ image, original };
    std::array<uint8_t, 4> pixels{ 41, 42, 43, 44 };
    const G1Element sprite{ .offset = pixels.data(), .width = 2, .height = 2 };
    GfxSetG1Element(image, &sprite);
    auto owner = std::make_shared<Ui::Vulkan::DeviceContextOwner>(false);
    auto service = Renderer::CreateConfiguredRenderServiceFactory(owner)->Create();
    auto request = Request();
    request.name = "native-world-service-lifecycle";
    request.logicalExtent = request.outputExtent = { 32, 32 };
    request.rgbaOutput = false;
    request.clearIndex = 10;

    auto materials = std::make_shared<TerrainPresentationMaterials>();
    materials->revision = GetTerrainObjectRevision();
    materials->surfaces[0] = { .imageBase = image, .imageCount = 1, .supported = true };
    auto entities = std::make_shared<EntityPresentationSnapshot>();
    entities->CaptureNativeStorage(getGameState().entities, {}, {});
    std::shared_ptr<MapPresentationSnapshot> map;
    const auto generation = [&](int32_t height) {
        MapPresentationChangeBatch batch;
        batch.epoch = 8111;
        batch.reset = map == nullptr;
        batch.profile = MapPublicationProfile::rawTerrain;
        batch.surfaceWidth = batch.surfaceHeight = 1;
        batch.sourceTick = entities->GetSourceTick();
        batch.terrainMaterials = materials;
        MapPresentationTileChange change;
        change.index = change.surfaceIndex = 0;
        change.surface.baseZ = static_cast<uint16_t>(height);
        change.surface.valid = 1;
        change.surface.terrain = { .baseZ = height, .kind = 1, .present = 1 };
        batch.changes.push_back(change);
        map = map ? std::make_shared<MapPresentationSnapshot>(*map) : std::make_shared<MapPresentationSnapshot>();
        map->Apply(batch);
        return std::make_shared<const PresentationGeneration>(PresentationGeneration{
            .map = map, .entities = entities, .sourceTick = batch.sourceTick,
            .sourceEntityEpoch = entities->GetSourceEpoch() });
    };
    const OrthographicCamera camera{ .viewX = -8, .viewY = -16, .clipRight = 32, .clipBottom = 32 };
    const auto record = [&](IRenderSession& session, const auto& scene) {
        auto& drawing = session.GetDrawingContext();
        const auto categories = drawing.DrawWorldScene(session.GetRenderTarget(), scene, camera);
        EXPECT_TRUE(categories.completeTerrainScene);
        EXPECT_TRUE(categories.surfaces);
        drawing.SealWorldScene(categories);
    };
    const auto expected = [&](int y, std::array<uint8_t, 4> values) {
        std::vector<std::byte> bytes(32 * 32, std::byte{ 10 });
        for (int row = 0; row < 2; ++row)
            for (int column = 0; column < 2; ++column)
                bytes[(y + row) * 32 + 8 + column] = std::byte{ values[row * 2 + column] };
        return bytes;
    };

    // Bitmap-only startup must not prevent later native world work in the same domain.
    auto bitmap = service->TryBeginOffscreen(request);
    ASSERT_NE(bitmap, nullptr);
    EXPECT_EQ(service->TryBeginOffscreen(request), nullptr);
    EXPECT_THROW(service->BeginOffscreen(request), RenderServiceException); // Nested recording cannot wait on itself.
    auto firstBitmap = bitmap->Submit();
    // Foreground admission is safe whether the previous GPU job has finished or is still running.
    auto first = service->BeginOffscreen(request);
    EXPECT_FALSE(firstBitmap->Wait(0ms).error.has_value());
    const auto initial = generation(0);
    record(*first, initial);
    const auto held = first->Submit()->Wait(120s);
    ASSERT_FALSE(held.error.has_value());
    ASSERT_NE(held.result, nullptr);
    EXPECT_EQ(held.result->indexed, expected(16, { 41, 42, 43, 44 }));
    EXPECT_TRUE(owner->IsCreated());

    auto second = service->BeginOffscreen(request);
    const auto moved = generation(8); // Same epoch: restarted per-session GPU revision counters would miss this change.
    EXPECT_EQ(initial->map->GetEpoch(), moved->map->GetEpoch());
    record(*second, moved);
    auto secondCompletion = second->Submit();
    auto third = service->BeginOffscreen(request); // Also proves native status retired before this slot is reused.
    const auto changed = secondCompletion->Wait(0ms);
    ASSERT_FALSE(changed.error.has_value());
    ASSERT_NE(changed.result, nullptr);
    EXPECT_EQ(changed.result->indexed, expected(8, { 41, 42, 43, 44 }));
    pixels = { 51, 52, 53, 54 };
    service->InvalidateImage(image);
    record(*third, moved);
    const auto recoloured = third->Submit()->Wait(120s);
    ASSERT_FALSE(recoloured.error.has_value());
    ASSERT_NE(recoloured.result, nullptr);
    EXPECT_EQ(recoloured.result->indexed, expected(8, { 51, 52, 53, 54 }));
    EXPECT_EQ(held.result->indexed, expected(16, { 41, 42, 43, 44 }));

    auto cancelled = service->BeginOffscreen(request);
    record(*cancelled, generation(16));
    cancelled->Cancel();
    auto retry = service->BeginOffscreen(request);
    record(*retry, initial);
    auto delivery = retry->Submit();
    delivery->Cancel(); // Delivery cancellation must not free the domain before the submitted frame retires.
    auto foreground = service->BeginOffscreen(request);
    record(*foreground, moved);
    const auto final = foreground->Submit()->Wait(120s);
    ASSERT_FALSE(final.error.has_value());
    ASSERT_NE(final.result, nullptr);
    EXPECT_EQ(final.result->indexed, expected(8, { 51, 52, 53, 54 }));
    service->Shutdown();
}

TEST_F(RenderServiceProductionRecordingTest, WorkerInvalidationAfterBeginRefreshesFirstSpriteUse)
{
    using namespace OpenRCT2;
    const auto image = SPR_TEMP_BEGIN;
    const auto original = *GfxGetG1Element(image);
    struct RestoreSprite
    {
        ImageIndex image;
        G1Element original;
        ~RestoreSprite() { GfxSetG1Element(image, &original); }
    } restore{ image, original };
    std::array<uint8_t, 2> pixels{ 42, 42 };
    const G1Element sprite{ .offset = pixels.data(), .width = 2, .height = 1 };
    GfxSetG1Element(image, &sprite);
    auto owner = std::make_shared<Ui::Vulkan::DeviceContextOwner>(false);
    auto service = Renderer::CreateConfiguredRenderServiceFactory(owner)->Create();
    auto request = Request();
    request.outputExtent = request.logicalExtent;
    request.rgbaOutput = false;
    auto first = service->BeginOffscreen(request);
    first->GetDrawingContext().DrawSprite(first->GetRenderTarget(), ImageId(image), 0, 0);
    auto completion = first->Submit();
    const auto held = completion->Wait(120s);
    ASSERT_FALSE(held.error.has_value());
    ASSERT_NE(held.result, nullptr);
    EXPECT_EQ(held.result->indexed, (std::vector<std::byte>{ std::byte{ 42 }, std::byte{ 42 } }));
    auto second = service->BeginOffscreen(request);
    // Models worker-generated scrolling text after BeginOffscreen but before serial sprite recording.
    pixels.fill(77);
    std::exception_ptr failure;
    std::thread worker([&] {
        try { service->InvalidateImage(image); }
        catch (...) { failure = std::current_exception(); }
    });
    worker.join();
    ASSERT_EQ(failure, nullptr);
    second->GetDrawingContext().DrawSprite(second->GetRenderTarget(), ImageId(image), 0, 0);
    const auto fresh = second->Submit()->Wait(120s);
    ASSERT_FALSE(fresh.error.has_value());
    ASSERT_NE(fresh.result, nullptr);
    EXPECT_EQ(fresh.result->indexed, (std::vector<std::byte>{ std::byte{ 77 }, std::byte{ 77 } }));
    EXPECT_EQ(held.result->indexed, (std::vector<std::byte>{ std::byte{ 42 }, std::byte{ 42 } }));
    service->Shutdown();
}

TEST_F(RenderServiceProductionRecordingTest, OrderedAliasRetainsZeroPoliciesAndSubmittedOwnership)
{
    using namespace OpenRCT2;
    auto owner = std::make_shared<Ui::Vulkan::DeviceContextOwner>(false);
    auto service = Renderer::CreateConfiguredRenderServiceFactory(owner)->Create();
    OffscreenRenderRequest request;
    request.name = "alias-owned-zero";
    request.logicalExtent = request.outputExtent = { 4, 1 };
    request.initialContents = RenderInitialContents::ownedIndices;
    request.initialIndices = { std::byte{ 0 }, std::byte{ 21 }, std::byte{ 22 }, std::byte{ 23 } };
    request.orderedAlias = OrderedImageAlias{ .destinationX = 1, .width = 3, .height = 1 };
    for (uint32_t i = 0; i < 256; ++i)
        request.orderedAlias->remap[i] = static_cast<uint8_t>(i);
    auto cancelled = service->BeginOffscreen(request);
    EXPECT_THROW(static_cast<void>(cancelled->GetRenderTarget()), RenderServiceException);
    EXPECT_THROW(static_cast<void>(cancelled->GetDrawingContext()), RenderServiceException);
    cancelled->Cancel();
    EXPECT_FALSE(owner->IsCreated());
    auto raw = service->BeginOffscreen(request);
    auto completion = raw->Submit();
    request.initialIndices[0] = std::byte{ 99 };
    request.orderedAlias->remap.fill(77);
    const auto first = completion->Wait(120s);
    ASSERT_FALSE(first.error.has_value());
    ASSERT_NE(first.result, nullptr);
    EXPECT_EQ(first.result->indexed, (std::vector<std::byte>(4, std::byte{ 0 })));

    request.initialIndices[0] = std::byte{ 0 };
    for (uint32_t i = 0; i < 256; ++i)
        request.orderedAlias->remap[i] = static_cast<uint8_t>(i);
    request.orderedAlias->skipSourceZero = true;
    request.orderedAlias->skipMappedZero = true;
    request.orderedAlias->remap[21] = 0;
    const auto remapped = service->BeginOffscreen(request)->Submit()->Wait(120s);
    ASSERT_FALSE(remapped.error.has_value());
    ASSERT_NE(remapped.result, nullptr);
    EXPECT_EQ(remapped.result->indexed,
        (std::vector<std::byte>{ std::byte{ 0 }, std::byte{ 21 }, std::byte{ 22 }, std::byte{ 22 } }));
    EXPECT_EQ(first.result->indexed, (std::vector<std::byte>(4, std::byte{ 0 })));
    service->Shutdown();
}
#endif
