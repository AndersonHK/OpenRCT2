/*****************************************************************************
 * Copyright (c) 2014-2026 OpenRCT2 developers
 *
 * For a complete list of all authors, please refer to contributors.md
 * Interested in contributing? Visit https://github.com/OpenRCT2/OpenRCT2
 *
 * OpenRCT2 is licensed under the GNU General Public License version 3.
 *****************************************************************************/

#if defined(OPENRCT2_TEST_UI_BINDINGS) && !defined(DISABLE_HTTP)

    #include <atomic>
    #include <chrono>
    #include <future>
    #include <gtest/gtest.h>
    #include <openrct2-ui/UiContext.h>
    #include <openrct2-ui/windows/ObjectDownloader.h>
    #include <openrct2/Context.h>
    #include <openrct2/OpenRCT2.h>
    #include <openrct2/PlatformEnvironment.h>
    #include <openrct2/audio/AudioContext.h>
    #include <openrct2/ui/UiContext.h>
    #include <openrct2/ui/WindowManager.h>
    #include <stdexcept>
    #include <thread>

using namespace OpenRCT2;
using namespace std::chrono_literals;

class ObjectDownloaderTest : public testing::Test
{
protected:
    std::unique_ptr<IContext> _context;

    void SetUp() override
    {
        gOpenRCT2Headless = true;
        gOpenRCT2NoGraphics = true;
        auto env = CreatePlatformEnvironment();
        auto ui = Ui::CreateUiContext(*env);
        _context = CreateContext(std::move(env), Audio::CreateDummyAudioContext(), std::move(ui));
        ASSERT_TRUE(_context->Initialise());
    }

    static ObjectEntryDescriptor Entry(std::string_view name)
    {
        RCTObjectEntry entry{};
        entry.SetName(name);
        entry.SetType(ObjectType::walls);
        return ObjectEntryDescriptor(entry);
    }

    static Http::Response Response(Http::Status status, std::string body = {})
    {
        Http::Response result;
        result.status = status;
        result.body = std::move(body);
        return result;
    }

    template<typename Done>
    bool Pump(Ui::Windows::ObjectDownloader* downloader, Done done)
    {
        const auto deadline = std::chrono::steady_clock::now() + 5s;
        do
        {
            _context->GetBackgroundWorker().dispatchCompleted();
            if (downloader != nullptr)
                downloader->Update();
            if (done())
                return true;
            std::this_thread::sleep_for(1ms);
        } while (std::chrono::steady_clock::now() < deadline);
        return false;
    }

    struct Gate
    {
        std::promise<void> release;
        std::shared_future<void> ready{ release.get_future().share() };
        ~Gate()
        {
            Open();
        }
        void Open()
        {
            try
            {
                release.set_value();
            }
            catch (const std::future_error&)
            {
            }
        }
    };

    struct DrainWorkerOnExit
    {
        BackgroundWorker& worker;
        ~DrainWorkerOnExit()
        {
            // Keep captured fixture state alive even when an ASSERT returns before a blocked request finishes.
            worker.shutdown();
        }
    };
};

TEST_F(ObjectDownloaderTest, FailuresAdvanceAndOnlySuccessfulInstallationsAreReportedOnMainThread)
{
    const auto mainThread = std::this_thread::get_id();
    std::atomic<int> requests{};
    int installations = 0;
    Ui::Windows::ObjectDownloader downloader(
        _context->GetBackgroundWorker(),
        [&](const Http::Request& request) {
            EXPECT_NE(std::this_thread::get_id(), mainThread);
            requests++;
            if (request.url.starts_with("test://"))
                return Response(request.url.ends_with("BADFILE") ? Http::Status::error : Http::Status::ok, "payload");
            const auto name = request.url.substr(request.url.find_last_of('/') + 1);
            if (name == "THROW")
                throw std::runtime_error("test transport failure");
            if (name == "HTTPFAIL")
                return Response(Http::Status::notFound);
            if (name == "BADJSON")
                return Response(Http::Status::ok, "{");
            if (name == "NOLINK")
                return Response(Http::Status::ok, "{}");
            return Response(
                Http::Status::ok, "{\"name\":\"" + name + "\",\"source\":\"test\",\"download\":\"test://" + name + "\"}");
        },
        [&](const ObjectEntryDescriptor&, const std::string& name, const std::string& body) {
            EXPECT_EQ(std::this_thread::get_id(), mainThread);
            EXPECT_EQ(body, "payload");
            installations++;
            return name == "GOOD";
        });
    DrainWorkerOnExit drain{ _context->GetBackgroundWorker() };
    downloader.Begin({ Entry("HTTPFAIL"), Entry("THROW"), Entry("BADJSON"), Entry("NOLINK"), Entry("BADFILE"),
                       Entry("SAVEFAIL"), Entry("GOOD") });
    ASSERT_TRUE(Pump(&downloader, [&] { return !downloader.IsDownloading(); }));
    EXPECT_EQ(requests, 10);
    EXPECT_EQ(installations, 2);
    EXPECT_EQ(downloader.GetDownloadedEntries(), std::vector<ObjectEntryDescriptor>{ Entry("GOOD") });
    downloader.Begin({});
    downloader.Update();
    EXPECT_FALSE(downloader.IsDownloading());
    EXPECT_TRUE(downloader.GetDownloadedEntries().empty());
    EXPECT_EQ(Ui::GetWindowManager()->FindByClass(WindowClass::networkStatus), nullptr);
}

TEST_F(ObjectDownloaderTest, StatusCancelAndRestartDiscardOldResponsesWithoutWaitingForNetwork)
{
    Gate gate;
    std::promise<void> started;
    auto startedFuture = started.get_future();
    std::promise<void> finished;
    auto finishedFuture = finished.get_future();
    std::atomic<int> requests{};
    Ui::Windows::ObjectDownloader downloader(
        _context->GetBackgroundWorker(),
        [&](const Http::Request& request) {
            requests++;
            if (request.url.ends_with("OLD"))
            {
                started.set_value();
                gate.ready.wait_for(3s);
                finished.set_value();
                return Response(Http::Status::ok, R"({"name":"OLD","download":"test://unexpected"})");
            }
            return Response(Http::Status::notFound);
        },
        [](const ObjectEntryDescriptor&, const std::string&, const std::string&) {
            ADD_FAILURE() << "A cancelled response must not install an object";
            return true;
        });
    DrainWorkerOnExit drain{ _context->GetBackgroundWorker() };
    downloader.Begin({ Entry("OLD") });
    downloader.Update();
    ASSERT_EQ(startedFuture.wait_for(2s), std::future_status::ready);
    ASSERT_NE(Ui::GetWindowManager()->FindByClass(WindowClass::networkStatus), nullptr);
    Ui::GetWindowManager()->CloseByClass(WindowClass::networkStatus);
    EXPECT_FALSE(downloader.IsDownloading());
    downloader.Begin({ Entry("NEW") });
    ASSERT_TRUE(Pump(&downloader, [&] { return !downloader.IsDownloading(); }));
    // The old request is still blocked, proving cancellation/restart did not join it on the UI thread.
    EXPECT_EQ(finishedFuture.wait_for(0s), std::future_status::timeout);
    gate.Open();
    ASSERT_EQ(finishedFuture.wait_for(2s), std::future_status::ready);
    ASSERT_TRUE(Pump(nullptr, [&] { return _context->GetBackgroundWorker().empty(); }));
    EXPECT_TRUE(downloader.GetDownloadedEntries().empty());
    EXPECT_EQ(requests, 2);
}

TEST_F(ObjectDownloaderTest, DestroyingControllerDiscardsLateObjectBodyAndLeavesStatusCallbackSafe)
{
    Gate gate;
    std::promise<void> started;
    auto startedFuture = started.get_future();
    std::promise<void> finished;
    auto finishedFuture = finished.get_future();
    int installations = 0;
    auto downloader = std::make_unique<Ui::Windows::ObjectDownloader>(
        _context->GetBackgroundWorker(),
        [&](const Http::Request& request) {
            if (request.url.starts_with("test://"))
            {
                started.set_value();
                gate.ready.wait_for(3s);
                finished.set_value();
                return Response(Http::Status::ok, "payload");
            }
            return Response(Http::Status::ok, R"({"name":"OLD","download":"test://object"})");
        },
        [&](const ObjectEntryDescriptor&, const std::string&, const std::string&) {
            installations++;
            return true;
        });
    DrainWorkerOnExit drain{ _context->GetBackgroundWorker() };
    downloader->Begin({ Entry("OLD") });
    ASSERT_TRUE(Pump(downloader.get(), [&] { return startedFuture.wait_for(0s) == std::future_status::ready; }));
    downloader.reset();
    EXPECT_EQ(finishedFuture.wait_for(0s), std::future_status::timeout);
    Ui::GetWindowManager()->CloseByClass(WindowClass::networkStatus);
    gate.Open();
    ASSERT_EQ(finishedFuture.wait_for(2s), std::future_status::ready);
    ASSERT_TRUE(Pump(nullptr, [&] { return _context->GetBackgroundWorker().empty(); }));
    EXPECT_EQ(installations, 0);
}

TEST_F(ObjectDownloaderTest, TruncatedDatIsNotPassedToRepository)
{
    Ui::Windows::ObjectDownloader downloader(_context->GetBackgroundWorker(), [](const Http::Request& request) {
        return Response(
            Http::Status::ok, request.url.starts_with("test://") ? "bad" : R"({"name":"SHORT","download":"test://object"})");
    });
    DrainWorkerOnExit drain{ _context->GetBackgroundWorker() };
    downloader.Begin({ Entry("SHORT") });
    ASSERT_TRUE(Pump(&downloader, [&] { return !downloader.IsDownloading(); }));
    EXPECT_TRUE(downloader.GetDownloadedEntries().empty());
}

#endif
