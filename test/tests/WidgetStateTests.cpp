/*****************************************************************************
 * Copyright (c) 2014-2026 OpenRCT2 developers
 *
 * For a complete list of all authors, please refer to contributors.md
 * Interested in contributing? Visit https://github.com/OpenRCT2/OpenRCT2
 *
 * OpenRCT2 is licensed under the GNU General Public License version 3.
 *****************************************************************************/

#include <gtest/gtest.h>
#include <openrct2-ui/interface/Widget.h>
#include <openrct2/core/FlagHolder.hpp>
#include <openrct2/interface/Widget.h>
#include <openrct2/interface/WindowBase.h>
#include <span>

#ifdef OPENRCT2_TEST_UI_BINDINGS
    #include <openrct2-ui/UiContext.h>
    #include <openrct2/Context.h>
    #include <openrct2/GameState.h>
    #include <openrct2/OpenRCT2.h>
    #include <openrct2/PlatformEnvironment.h>
    #include <openrct2/audio/AudioContext.h>
    #include <openrct2/interface/Window.h>
    #include <openrct2/interface/WindowClasses.h>
    #include <openrct2/ui/UiContext.h>
    #include <openrct2/ui/WindowManager.h>
#endif

using namespace OpenRCT2;

#ifdef OPENRCT2_TEST_UI_BINDINGS
TEST(WidgetStateTest, SplitHudResizesWithEitherInfoPanelAbsentAndPreservesNoMoneyControls)
{
    gOpenRCT2Headless = true;
    gOpenRCT2NoGraphics = true;
    auto env = CreatePlatformEnvironment();
    auto uiContext = Ui::CreateUiContext(*env);
    auto context = CreateContext(std::move(env), Audio::CreateDummyAudioContext(), std::move(uiContext));
    ASSERT_TRUE(context->Initialise());
    auto* manager = Ui::GetWindowManager();
    manager->CloseByClass(WindowClass::progressWindow);
    auto* news = manager->OpenWindow(WindowClass::bottomToolbar);
    ASSERT_NE(news, nullptr);
    auto* options = manager->Create<WindowBase>(WindowClass::options, ScreenCoordsXY{}, ScreenSize{ 200, 100 }, {});
    auto* progress = manager->Create<WindowBase>(WindowClass::progressWindow, ScreenCoordsXY{}, ScreenSize{ 100, 40 }, {});
    ASSERT_NE(options, nullptr);
    ASSERT_NE(progress, nullptr);
    ASSERT_EQ(manager->FindByClass(WindowClass::progressWindow), progress);
    for (bool havePark : { false, true })
    {
        for (bool haveDate : { false, true })
        {
            SCOPED_TRACE(havePark);
            SCOPED_TRACE(haveDate);
            manager->CloseByClass(WindowClass::parkInfoPanel);
            manager->CloseByClass(WindowClass::dateInfoPanel);
            auto* park = havePark ? manager->OpenWindow(WindowClass::parkInfoPanel) : nullptr;
            auto* date = haveDate ? manager->OpenWindow(WindowClass::dateInfoPanel) : nullptr;
            ASSERT_EQ(park != nullptr, havePark);
            ASSERT_EQ(date != nullptr, haveDate);
            for (const auto size : { ScreenSize{ 640, 480 }, ScreenSize{ 1001, 701 } })
            {
                WindowResizeGui(size.width, size.height);
                EXPECT_EQ(news->windowPos.x, havePark ? 142 : 0);
                EXPECT_EQ(news->width, size.width - (havePark ? 142 : 0) - (haveDate ? 142 : 0));
                EXPECT_EQ(news->windowPos.y, size.height - 32);
                if (park != nullptr)
                    EXPECT_EQ(park->windowPos.y, size.height - 32);
                if (date != nullptr)
                {
                    EXPECT_EQ(date->windowPos.x, size.width - 142);
                    EXPECT_EQ(date->windowPos.y, size.height - 32);
                }
                EXPECT_EQ(options->windowPos, (ScreenCoordsXY{ (size.width - 200) / 2, (size.height - 100) / 2 }));
                EXPECT_EQ(progress->windowPos, (ScreenCoordsXY{ (size.width - 100) / 2, (size.height - 40) / 2 }));
            }
            if (park != nullptr)
            {
                for (bool noMoney : { false, true, false })
                {
                    getGameState().park.flags.set(ParkFlag::noMoney, noMoney);
                    park->onPrepareDraw();
                    EXPECT_EQ(park->widgets[2].isVisible(), !noMoney);
                    EXPECT_TRUE(park->widgets[3].isVisible());
                    EXPECT_TRUE(park->widgets[4].isVisible());
                }
            }
        }
    }
}
#endif

// A hidden control can still change representation (for example, target-price dropdown versus shop spinner).
TEST(WidgetStateTest, HiddenControlPreservesUpdatedRepresentationAndInteractionState)
{
    Widget widget{};
    widget.type = WidgetType::spinner;
    widget.flags.set(WidgetFlag::isHoldable);
    widget.flags.set(WidgetFlag::isDisabled);
    widget.setHidden();
    widget.type = WidgetType::dropdownMenu;
    widget.setString("target price");
    widget.setVisible();

    EXPECT_TRUE(widget.isVisible());
    EXPECT_EQ(widget.type, WidgetType::dropdownMenu);
    EXPECT_STREQ(widget.string, "target price");
    EXPECT_TRUE(widget.flags.has(WidgetFlag::textIsString));
    EXPECT_TRUE(widget.flags.has(WidgetFlag::isHoldable));
    EXPECT_TRUE(widget.flags.has(WidgetFlag::isDisabled));
}

TEST(WidgetStateTest, WidgetDefaultConstructedHasNoFlags)
{
    Widget widget{};
    ASSERT_TRUE(widget.flags.isEmpty());
    ASSERT_FALSE(widget.flags.has(WidgetFlag::isPressed));
    ASSERT_FALSE(widget.flags.has(WidgetFlag::isDisabled));
    ASSERT_FALSE(widget.flags.has(WidgetFlag::isHoldable));
}

TEST(WidgetStateTest, FlagHolderSetConditional)
{
    WidgetFlags flags;
    flags.set(WidgetFlag::isPressed, true);
    ASSERT_TRUE(flags.has(WidgetFlag::isPressed));
    flags.set(WidgetFlag::isPressed, false);
    ASSERT_FALSE(flags.has(WidgetFlag::isPressed));
}

TEST(WidgetStateTest, FlagsAreIndependent)
{
    WidgetFlags flags;
    flags.set(WidgetFlag::isPressed);
    flags.set(WidgetFlag::isHoldable);
    ASSERT_TRUE(flags.has(WidgetFlag::isPressed));
    ASSERT_TRUE(flags.has(WidgetFlag::isHoldable));
    ASSERT_FALSE(flags.has(WidgetFlag::isDisabled));
    flags.unset(WidgetFlag::isPressed);
    ASSERT_FALSE(flags.has(WidgetFlag::isPressed));
    ASSERT_TRUE(flags.has(WidgetFlag::isHoldable));
}

// Regression guard for #26421: widgets at index >= 64 must round-trip cleanly.
// The scenery window's 65th tab has widget index 79. A uint64 bitmask indexed
// by widget index would wrap (1uLL << 79 == 1uLL << 15) and cause the first
// tab to falsely report as pressed. Per-widget flags have no such cap.
TEST(WidgetStateTest, HighIndexRoundTripsWithoutShiftWrap)
{
    WindowBase w;
    w.widgets.resize(300);
    w.widgets[79].flags.set(WidgetFlag::isPressed);

    ASSERT_TRUE(w.widgets[79].flags.has(WidgetFlag::isPressed));
    // Must not false-positive at any prior shift-wrap alias.
    ASSERT_FALSE(w.widgets[15].flags.has(WidgetFlag::isPressed));  // 79 % 64
    ASSERT_FALSE(w.widgets[143].flags.has(WidgetFlag::isPressed)); // 143 % 64 == 15
    ASSERT_FALSE(w.widgets[271].flags.has(WidgetFlag::isPressed));
}

TEST(WidgetStateTest, SetWidgetsPreservesFlags)
{
    Widget mutableSource[3] = { Widget{}, Widget{}, Widget{} };
    mutableSource[1].flags.set(WidgetFlag::isPressed);
    mutableSource[1].flags.set(WidgetFlag::isHoldable);

    WindowBase w;
    w.setWidgets(std::span<const Widget>(mutableSource, 3));

    ASSERT_EQ(w.widgets.size(), 3u);
    ASSERT_TRUE(w.widgets[1].flags.has(WidgetFlag::isPressed));
    ASSERT_TRUE(w.widgets[1].flags.has(WidgetFlag::isHoldable));
    ASSERT_FALSE(w.widgets[0].flags.has(WidgetFlag::isPressed));
    ASSERT_FALSE(w.widgets[2].flags.has(WidgetFlag::isPressed));
}

TEST(WidgetStateTest, WithFlagSetsFlagWithoutDisturbingOthers)
{
    using Ui::withFlag;

    Widget base{};
    base.flags.set(WidgetFlag::isPressed);

    auto holdable = withFlag(base, WidgetFlag::isHoldable);
    ASSERT_TRUE(holdable.flags.has(WidgetFlag::isPressed));
    ASSERT_TRUE(holdable.flags.has(WidgetFlag::isHoldable));
    ASSERT_FALSE(holdable.flags.has(WidgetFlag::isDisabled));
}

TEST(WidgetStateTest, MakeHoldableWidgetHasHoldableFlag)
{
    using Ui::makeHoldableWidget;
    using Ui::WindowColour;

    constexpr auto w = makeHoldableWidget({ 0, 0 }, { 10, 10 }, WidgetType::button, WindowColour::primary);
    static_assert(w.flags.has(WidgetFlag::isHoldable));
    static_assert(!w.flags.has(WidgetFlag::isPressed));
    static_assert(!w.flags.has(WidgetFlag::isDisabled));
    ASSERT_TRUE(w.flags.has(WidgetFlag::isHoldable));
}

TEST(WidgetStateTest, MakeWidgetDefaultHasNoFlags)
{
    using Ui::makeWidget;
    using Ui::WindowColour;

    constexpr auto w = makeWidget({ 0, 0 }, { 10, 10 }, WidgetType::button, WindowColour::primary);
    static_assert(w.flags.isEmpty());
    ASSERT_TRUE(w.flags.isEmpty());
}

TEST(WidgetStateTest, MakeSpinnerWidgetsHasNoHoldableFlag)
{
    using Ui::makeSpinnerWidgets;
    using Ui::WindowColour;

    constexpr auto widgets = makeSpinnerWidgets({ 0, 0 }, { 100, 14 }, WidgetType::spinner, WindowColour::primary);
    static_assert(!widgets[0].flags.has(WidgetFlag::isHoldable));
    static_assert(!widgets[1].flags.has(WidgetFlag::isHoldable));
    static_assert(!widgets[2].flags.has(WidgetFlag::isHoldable));
}

TEST(WidgetStateTest, MakeHoldableSpinnerWidgetsHasHoldableOnIncrementButtons)
{
    using Ui::makeHoldableSpinnerWidgets;
    using Ui::WindowColour;

    constexpr auto widgets = makeHoldableSpinnerWidgets({ 0, 0 }, { 100, 14 }, WidgetType::spinner, WindowColour::primary);
    // Element [0] is the spinner input field; increment/decrement buttons are
    // [1] and [2] and must be holdable for press-and-hold repeat.
    static_assert(!widgets[0].flags.has(WidgetFlag::isHoldable));
    static_assert(widgets[1].flags.has(WidgetFlag::isHoldable));
    static_assert(widgets[2].flags.has(WidgetFlag::isHoldable));
}
