/*****************************************************************************
 * Copyright (c) 2014-2026 OpenRCT2 developers
 *
 * For a complete list of all authors, please refer to contributors.md
 * Interested in contributing? Visit https://github.com/OpenRCT2/OpenRCT2
 *
 * OpenRCT2 is licensed under the GNU General Public License version 3.
 *****************************************************************************/

#include "Ui.h"

#include "SDLException.h"
#include "UiContext.h"
#include "audio/AudioContext.h"
#include "drawing/BitmapReader.h"

#include <memory>
#include <string>
#include <openrct2/Context.h>
#include <openrct2/Diagnostic.h>
#include <openrct2/OpenRCT2.h>
#include <openrct2/PlatformEnvironment.h>
#include <openrct2/audio/AudioContext.h>
#include <openrct2/command_line/CommandLine.hpp>
#include <openrct2/command_line/ExitCode.h>
#include <openrct2/platform/Platform.h>
#include <openrct2/ui/UiContext.h>

#ifdef __EMSCRIPTEN__
    #include <emscripten.h>
#endif

using namespace OpenRCT2;
using namespace OpenRCT2::Audio;
using namespace OpenRCT2::Ui;

template<typename T>
static std::shared_ptr<T> ToShared(std::unique_ptr<T>&& src)
{
    return std::shared_ptr<T>(std::move(src));
}

/**
 * Main entry point for non-Windows systems. Windows instead uses its own DLL proxy.
 */
#if defined(_MSC_VER) && !defined(__DISABLE_DLL_PROXY__)
int NormalisedMain(int argc, const char** argv)
#else
int main(int argc, const char** argv)
#endif
{
#ifdef __EMSCRIPTEN__
    MAIN_THREAD_EM_ASM({
        specialHTMLTargets["!canvas"] = Module.canvas;
        Module.canvas.addEventListener("contextmenu", function(e) { e.preventDefault(); });
    });
#endif
    int32_t rc = EXIT_SUCCESS;
    auto runGame = CommandLineRun(argv, argc);
    RegisterBitmapReader();
    if (runGame == OpenRCT2::CommandLine::ExitCode::launch)
    {
        std::unique_ptr<IContext> context;
        try
        {
            if (gOpenRCT2Headless)
            {
                // Run OpenRCT2 with a plain context
                context = CreateContext();
            }
            else
            {
                // Run OpenRCT2 with a UI context
                auto env = CreatePlatformEnvironment();
                std::unique_ptr<IAudioContext> audioContext;
                if (gIntegratedBenchmark.enabled)
                {
                    // The integrated benchmark exercises the full renderer without producing meeting-disrupting park audio.
                    audioContext = CreateDummyAudioContext();
                }
                else
                {
                    try
                    {
                        audioContext = CreateAudioContext();
                    }
                    catch (const SDLException& e)
                    {
                        LOG_WARNING(
                            "Failed to create audio context. Using dummy audio context. Error message was: %s", e.what());
                        audioContext = CreateDummyAudioContext();
                    }
                }
                auto uiContext = CreateUiContext(*env);
                context = CreateContext(std::move(env), std::move(audioContext), std::move(uiContext));
            }
            rc = context->RunOpenRCT2(argc, argv);
        }
        catch (const std::exception& e)
        {
            const std::string message = std::string("OpenRCT2 could not continue:\n\n") + e.what();
            LOG_ERROR("Unhandled startup or runtime error: %s", e.what());
            if (context != nullptr && !gOpenRCT2Headless && !gIntegratedBenchmark.enabled)
            {
                try
                {
                    context->GetUiContext().ShowMessageBox(message);
                }
                catch (...)
                {
                    LOG_ERROR("Unable to display the startup error message box.");
                }
            }
            rc = EXIT_FAILURE;
        }
        catch (...)
        {
            LOG_ERROR("Unhandled non-standard startup or runtime error.");
            if (context != nullptr && !gOpenRCT2Headless && !gIntegratedBenchmark.enabled)
            {
                try
                {
                    context->GetUiContext().ShowMessageBox("OpenRCT2 could not continue because of an unknown error.");
                }
                catch (...)
                {
                    LOG_ERROR("Unable to display the startup error message box.");
                }
            }
            rc = EXIT_FAILURE;
        }
    }
    else if (runGame == OpenRCT2::CommandLine::ExitCode::fail)
    {
        rc = EXIT_FAILURE;
    }
    return rc;
}

#ifdef __ANDROID__
extern "C" {
int SDL_main(int argc, const char* argv[])
{
    return main(argc, argv);
}
}
#endif
