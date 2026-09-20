/*****************************************************************************
 * Copyright (c) 2014-2026 OpenRCT2 developers
 * OpenRCT2 is licensed under the GNU General Public License version 3.
 *****************************************************************************/

#pragma once

#include <SDL.h>
#include <functional>
#include <iterator>
#include <openrct2-ui/interface/Widget.h>
#include <openrct2-ui/interface/Window.h>
#include <openrct2-ui/windows/Windows.h>
#include <openrct2/Input.h>
#include <openrct2/core/Json.hpp>
#include <openrct2/interface/Window.h>
#include <openrct2/ui/WindowManager.h>
#include <stdexcept>
#include <string>
#include <vector>

namespace OpenRCT2::UiParityFixtures
{
    inline void Require(bool condition, const char* message)
    {
        if (!condition)
            throw std::runtime_error(message);
    }

    inline std::vector<std::string> FixtureSteps(const std::string& family)
    {
        if (family == "baseline")
            return { "baseline" };
        if (family == "overlap")
            return { "empty-ui", "research-front", "finances-front", "partially-clipped", "research-only", "restored" };
        if (family == "scroll")
            return { "empty-ui", "scroll-top", "scroll-partial-row", "scroll-screen-clip", "scroll-resized", "restored" };
        if (family == "text")
            return { "empty-ui", "text-empty", "wrapped-caret-start", "wrapped-caret-end", "submitted", "restored" };
        throw std::invalid_argument("--fixture must be baseline, overlap, scroll or text");
    }

    inline json_t WindowInputState()
    {
        auto windows = json_t::array();
        WindowVisitEach([&](WindowBase* window) {
            auto scrolls = json_t::array();
            size_t scrollIndex = 0;
            for (size_t index = 0; index < window->widgets.size(); index++)
            {
                const auto& widget = window->widgets[index];
                if (widget.type != WidgetType::scroll)
                    continue;
                Require(scrollIndex < std::size(window->scrolls), "Unexpected scroll widget count");
                const auto& scroll = window->scrolls[scrollIndex++];
                scrolls.push_back({ { "widgetIndex", index },
                                    { "bounds", { widget.left, widget.top, widget.right, widget.bottom } },
                                    { "offset", { scroll.contentOffsetX, scroll.contentOffsetY } },
                                    { "contentSize", { scroll.contentWidth, scroll.contentHeight } },
                                    { "verticalThumb", { scroll.vThumbTop, scroll.vThumbBottom } },
                                    { "verticalScrollbar", scroll.flags.has(ScrollFlag::vScrollbarVisible) } });
            }
            windows.push_back({ { "class", static_cast<int32_t>(window->classification) },
                                { "number", window->number },
                                { "rectangle", { window->windowPos.x, window->windowPos.y, window->width, window->height } },
                                { "page", window->page },
                                { "scrolls", scrolls } });
        });
        return { { "windows", windows }, { "invalidation", "full-screen" } };
    }

    using FixtureCapture = std::function<void(const std::string&, json_t)>;

    inline void RunFixture(const std::string& family, const FixtureCapture& capture)
    {
        using namespace Ui::Windows;
        auto* manager = Ui::GetWindowManager();
        const auto close = [&](WindowBase* window) {
            Require(window != nullptr, "Cannot close a missing fixture window");
            const auto cls = window->classification;
            const auto number = window->number;
            manager->Close(*window);
            WindowCullDead();
            Require(manager->FindByNumber(cls, number) == nullptr, "Fixture window remained after closing");
        };
        const auto sample = [&](const std::string& step, json_t detail = json_t::object()) {
            Require(WindowGetMain() != nullptr && WindowGetMain()->viewport != nullptr, "Main viewport lost during fixture");
            // Settle production widget layout before recording the exact input geometry used by Paint.
            WindowVisitEach([](WindowBase* window) { window->onPrepareDraw(); });
            auto state = WindowInputState();
            state["detail"] = std::move(detail);
            capture(step, std::move(state));
        };
        if (family == "baseline")
        {
            // Preserve the original baseline's paint sequence; it needs no fixture layout preparation.
            auto state = WindowInputState();
            state["detail"] = json_t::object();
            capture("baseline", std::move(state));
            return;
        }
        sample("empty-ui");
        if (family == "overlap")
        {
            auto* finances = FinancesOpen();
            Require(finances != nullptr, "Finances window failed to open");
            WindowSetPosition(*finances, { 64, 72 });
            auto* research = ResearchOpen();
            Require(research != nullptr, "Research window failed to open");
            WindowSetPosition(*research, { 260, 132 });
            const ScreenCoordsXY intersection{ 280, 160 };
            Require(
                intersection.x < finances->windowPos.x + finances->width
                    && intersection.y < finances->windowPos.y + finances->height,
                "Fixture windows do not overlap at the chosen observation point");
            Require(manager->FindFromPoint(intersection) == research, "Research window is not frontmost in the overlap");
            sample("research-front", { { "frontClass", static_cast<int32_t>(research->classification) } });
            manager->BringToFront(*finances);
            Require(manager->FindFromPoint(intersection) == finances, "Finances window did not move to the front");
            sample("finances-front", { { "frontClass", static_cast<int32_t>(finances->classification) } });
            WindowSetPosition(*finances, { -24, 96 });
            Require(
                finances->windowPos.x == -24 && finances->windowPos.x + finances->width > 0,
                "Window did not retain a partially clipped rectangle");
            sample("partially-clipped");
            close(finances);
            sample("research-only");
            close(research);
        }
        else if (family == "scroll")
        {
            auto* window = ShortcutKeysOpen();
            Require(window != nullptr, "Shortcut list failed to open");
            WindowSetPosition(*window, { 90, 80 });
            window->onResize();
            window->onPrepareDraw();
            WidgetIndex scrollWidget = 0;
            size_t scrollCount = 0;
            for (WidgetIndex index = 0; index < window->widgets.size(); index++)
            {
                if (window->widgets[index].type == WidgetType::scroll)
                {
                    scrollWidget = index;
                    scrollCount++;
                }
            }
            Require(scrollCount == 1, "Shortcut list must have exactly one discoverable scroll widget");
            WindowUpdateScrollWidgets(*window);
            auto& scroll = window->scrolls[0];
            const auto& widget = window->widgets[scrollWidget];
            Require(
                scroll.flags.has(ScrollFlag::vScrollbarVisible) && scroll.contentHeight > widget.bottom - widget.top + 37
                    && scroll.contentOffsetY == 0,
                "Shortcut list did not provide the required vertical overflow");
            sample("scroll-top");
            const auto initialThumb = scroll.vThumbTop;
            scroll.contentOffsetY = 37;
            WindowUpdateScrollWidgets(*window);
            Ui::widgetScrollUpdateThumbs(*window, scrollWidget);
            window->invalidate();
            Require(scroll.contentOffsetY == 37 && scroll.vThumbTop != initialThumb, "Scroll offset or thumb did not advance");
            sample("scroll-partial-row");
            WindowSetPosition(*window, { -28, 110 });
            Require(window->windowPos.x == -28, "Shortcut window did not retain negative screen clipping");
            sample("scroll-screen-clip");
            const auto oldWidth = window->width;
            const auto oldHeight = window->height;
            WindowResizeByDelta(*window, 80, 40);
            Require(
                window->width == oldWidth + 80 && window->height == oldHeight + 40,
                "Shortcut list did not resize by the requested delta");
            sample("scroll-resized");
            close(window);
        }
        else if (family == "text")
        {
            auto* research = ResearchOpen();
            Require(research != nullptr, "Research window behind text input failed to open");
            WindowSetPosition(*research, { 260, 132 });
            int submitted = 0;
            int cancelled = 0;
            std::string received;
            const auto openText = [&](const std::string& text) {
                WindowTextInputOpen(
                    "Renderer parity text", "A fixed description for wrapped text and caret placement.", text, 256,
                    [&](std::string_view result) {
                        submitted++;
                        received = result;
                    },
                    [&]() { cancelled++; });
                auto* dialog = manager->FindByClass(WindowClass::textinput);
                Require(dialog != nullptr, "Text input dialog failed to open");
                dialog->onPrepareDraw();
                const auto* session = GetTextboxSession();
                Require(
                    session != nullptr && session->Buffer != nullptr && *session->Buffer == text,
                    "Text input session did not retain the prescribed text");
                return dialog;
            };
            auto* empty = openText("");
            const auto emptyHeight = empty->height;
            sample("text-empty", { { "text", "" }, { "caret", 0 }, { "dialogUpdates", 0 } });
            close(empty);
            std::string text;
            for (int i = 0; i < 4; i++)
                text += "Ferris wheel entrance beside the garden path. ";
            auto* dialog = openText(text);
            Require(dialog->height > emptyHeight, "Long text did not produce a taller wrapped dialog");
            SetTextboxCaret(0);
            for (int i = 0; i < 16; i++)
                dialog->onUpdate();
            sample("wrapped-caret-start", { { "text", text }, { "caret", 0 }, { "dialogUpdates", 16 } });
            SetTextboxCaret(static_cast<int64_t>(text.size()));
            Require(GetTextboxSession()->SelectionStart == text.size(), "Caret did not move to the requested UTF-8 offset");
            dialog->invalidate();
            sample("wrapped-caret-end", { { "text", text }, { "caret", text.size() }, { "dialogUpdates", 16 } });
            WindowTextInputKey(dialog, SDLK_RETURN);
            WindowCullDead();
            Require(
                submitted == 1 && cancelled == 0 && received == text && manager->FindByClass(WindowClass::textinput) == nullptr,
                "Text submission did not complete exactly once with the prescribed text");
            sample("submitted", { { "submitted", submitted }, { "cancelled", cancelled }, { "received", received } });
            close(research);
        }
        sample("restored");
    }
} // namespace OpenRCT2::UiParityFixtures
