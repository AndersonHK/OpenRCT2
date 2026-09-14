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
    #include <openrct2-ui/input/MouseInput.h>
    #include <openrct2-ui/interface/Dropdown.h>
    #include <openrct2-ui/input/ShortcutIds.h>
    #include <openrct2-ui/input/ShortcutManager.h>
    #include <openrct2-ui/windows/Windows.h>
    #include <openrct2/Context.h>
    #include <openrct2/GameState.h>
    #include <openrct2/Input.h>
    #include <openrct2/OpenRCT2.h>
    #include <openrct2/PlatformEnvironment.h>
    #include <openrct2/audio/AudioContext.h>
    #include <openrct2/config/Config.h>
    #include <openrct2/entity/Guest.h>
    #include <openrct2/interface/WidgetIndexGlobals.h>
    #include <openrct2/interface/Window.h>
    #include <openrct2/interface/WindowClasses.h>
    #include <openrct2/scenes/editor/EditorController.h>
    #include <openrct2/scenes/editor/EditorStep.h>
    #include <openrct2/ui/UiContext.h>
    #include <openrct2/ui/WindowManager.h>
#endif

using namespace OpenRCT2;

#ifdef OPENRCT2_TEST_UI_BINDINGS
    #include <SDL.h>
TEST(WidgetStateTest, QueuedOutsideMouseReleaseClampsDraggingAndClosesOrphanDropdowns)
{
    gOpenRCT2Headless = true;
    gOpenRCT2NoGraphics = true;
    auto env = CreatePlatformEnvironment();
    auto uiContext = Ui::CreateUiContext(*env);
    auto context = CreateContext(std::move(env), Audio::CreateDummyAudioContext(), std::move(uiContext));
    ASSERT_TRUE(context->Initialise());
    struct RestoreInput
    {
        InputFlags flags{ gInputFlags };
        decltype(gHoverWidget) hover{ gHoverWidget };
        decltype(Config::Get().general.windowScale) scale{ Config::Get().general.windowScale };
        int32_t width{ Config::Get().general.windowWidth };
        int32_t height{ Config::Get().general.windowHeight };
        bool startedEvents{ SDL_WasInit(SDL_INIT_EVENTS) == 0 };
        ~RestoreInput()
        {
            InputSetState(InputState::reset);
            gInputFlags = flags;
            gHoverWidget = hover;
            Config::Get().general.windowScale = scale;
            Config::Get().general.windowWidth = width;
            Config::Get().general.windowHeight = height;
            if (startedEvents) SDL_QuitSubSystem(SDL_INIT_EVENTS);
        }
    } restoreInput;
    if (restoreInput.startedEvents) ASSERT_EQ(SDL_InitSubSystem(SDL_INIT_EVENTS), 0);
    Config::Get().general.windowScale = 1;
    Config::Get().general.windowWidth = 640;
    Config::Get().general.windowHeight = 480;
    SDL_Event resize{};
    resize.type = SDL_WINDOWEVENT;
    resize.window.event = SDL_WINDOWEVENT_RESIZED;
    resize.window.data1 = 640;
    resize.window.data2 = 480;
    ASSERT_EQ(SDL_PushEvent(&resize), 1);
    context->GetUiContext().ProcessMessages();
    ASSERT_EQ(ContextGetWidth(), 640);
    ASSERT_EQ(ContextGetHeight(), 480);
    gInputFlags = {};
    auto* manager = Ui::GetWindowManager();
    auto* parent = manager->OpenWindow(WindowClass::finances);
    ASSERT_NE(parent, nullptr);
    parent->flags.set(WindowFlag::noSnapping);
    for (const int32_t pointerX : { -1000, 2000 })
    {
        InputWindowPositionBegin(*parent, 0, parent->windowPos + ScreenCoordsXY{ 10, 10 });
        StoreMouseInput(MouseState::leftRelease, { pointerX, 150 });
        GameHandleInput();
        EXPECT_EQ(parent->windowPos.x, pointerX < 0 ? -10 : 629);
        EXPECT_EQ(parent->windowPos.y, 140);
        EXPECT_EQ(InputGetState(), InputState::normal);
    }
    // Remember a summary-page widget, then switch to a shorter graph page before the next hover update.
    parent->windowPos = { 100, 100 };
    parent->onMouseUp(4);
    parent->onPrepareDraw();
    const auto summaryLastWidget = static_cast<WidgetIndex>(parent->widgets.size() - 1);
    parent->onMouseUp(5);
    parent->onPrepareDraw();
    ASSERT_GE(summaryLastWidget, parent->widgets.size());
    for (const WidgetIndex staleIndex : { summaryLastWidget, kWidgetIndexNull })
    {
        gHoverWidget.windowClassification = parent->classification;
        gHoverWidget.windowNumber = parent->number;
        gHoverWidget.widgetIndex = staleIndex;
        InputSetState(InputState::normal);
        StoreMouseInput(MouseState::released, parent->windowPos + ScreenCoordsXY{ 50, 5 });
        GameHandleInput();
        EXPECT_EQ(gHoverWidget.windowClassification, parent->classification);
        EXPECT_LT(gHoverWidget.widgetIndex, parent->widgets.size());
    }
    const auto openDropdown = [&]() {
        gPressedWidget.windowClassification = parent->classification;
        gPressedWidget.windowNumber = parent->number;
        gPressedWidget.widgetIndex = 0;
        Ui::Windows::WindowDropdownShowTextCustomWidth({ 100, 100 }, 0, parent->colours[0], 12, {}, size_t{ 1 }, 60);
    };
    openDropdown();
    ASSERT_NE(manager->FindByClass(WindowClass::dropdown), nullptr);
    StoreMouseInput(MouseState::leftRelease, { -1000, -1000 });
    GameHandleInput();
    EXPECT_EQ(manager->FindByClass(WindowClass::dropdown), nullptr);
    EXPECT_EQ(InputGetState(), InputState::normal);
    openDropdown();
    ASSERT_NE(manager->FindByClass(WindowClass::dropdown), nullptr);
    manager->CloseByClass(WindowClass::finances);
    StoreMouseInput(MouseState::leftRelease, { -1000, -1000 });
    GameHandleInput();
    EXPECT_EQ(manager->FindByClass(WindowClass::dropdown), nullptr);
    EXPECT_NE(InputGetState(), InputState::dropdownActive);
}

TEST(WidgetStateTest, EditorPanelsPreserveStepVisibilityResizeAndToolbarToggle)
{
    gOpenRCT2Headless = true;
    gOpenRCT2NoGraphics = true;
    auto env = CreatePlatformEnvironment();
    auto uiContext = Ui::CreateUiContext(*env);
    auto context = CreateContext(std::move(env), Audio::CreateDummyAudioContext(), std::move(uiContext));
    ASSERT_TRUE(context->Initialise());
    const auto previousScene = gLegacyScene;
    struct RestoreScene
    {
        LegacyScene scene;
        ~RestoreScene() { gLegacyScene = scene; }
    } restoreScene{ previousScene };
    gLegacyScene = LegacyScene::scenarioEditor;
    auto& state = getGameState();
    state.entities.resetAllEntities();
    state.park.flags.unset(ParkFlag::spritesInitialised);
    auto* manager = Ui::GetWindowManager();
    auto* previous = manager->OpenWindow(WindowClass::editorStepController);
    auto* status = manager->OpenWindow(WindowClass::editorStatusLine);
    auto* next = manager->OpenWindow(WindowClass::editorStepController);
    ASSERT_NE(previous, nullptr);
    ASSERT_NE(status, nullptr);
    ASSERT_NE(next, nullptr);
    EXPECT_NE(previous, next);
    EXPECT_EQ(previous->number, 0);
    EXPECT_EQ(next->number, 1);
    EXPECT_EQ(manager->OpenWindow(WindowClass::editorStepController), next);
    EXPECT_EQ(manager->OpenWindow(WindowClass::editorStatusLine), status);
    for (int step = 0; step <= 8; step++)
    {
        state.editorStep = static_cast<Editor::Step>(step);
        gLegacyScene = step == 8 ? LegacyScene::trackDesignsManager
            : step == 7 ? LegacyScene::trackDesigner : LegacyScene::scenarioEditor;
        previous->onPrepareDraw();
        next->onPrepareDraw();
        EXPECT_EQ(previous->widgets[1].isVisible(), step != 0 && step != 6 && step != 8);
        EXPECT_EQ(next->widgets[1].isVisible(), step != 6 && step != 7 && step != 8);
        previous->onMouseUp(0); // The decorative image must not navigate.
        next->onMouseUp(0);
        EXPECT_EQ(state.editorStep, static_cast<Editor::Step>(step));
        if (step == 6 || step == 8)
        {
            previous->onMouseUp(1);
            next->onMouseUp(1);
            EXPECT_EQ(state.editorStep, static_cast<Editor::Step>(step));
        }
    }
    gLegacyScene = LegacyScene::scenarioEditor;
    state.editorStep = Editor::Step::optionsSelection;
    state.park.flags.set(ParkFlag::spritesInitialised);
    previous->onPrepareDraw();
    EXPECT_FALSE(previous->widgets[1].isVisible());
    previous->onMouseUp(1);
    EXPECT_EQ(state.editorStep, Editor::Step::optionsSelection);
    state.editorStep = Editor::Step::invalid;
    EXPECT_EQ(Editor::getStepStringId(state.editorStep), kStringIdNone);
    previous->onMouseUp(1);
    next->onMouseUp(1);
    EXPECT_EQ(state.editorStep, Editor::Step::invalid);
    state.editorStep = Editor::Step::landscapeEditor;
    for (const auto size : { ScreenSize{ 640, 480 }, ScreenSize{ 1001, 701 } })
    {
        WindowResizeGui(size.width, size.height);
        EXPECT_EQ(previous->windowPos, (ScreenCoordsXY{ 0, size.height - previous->height }));
        EXPECT_EQ(next->windowPos, (ScreenCoordsXY{ size.width - next->width, size.height - next->height }));
        EXPECT_EQ(status->windowPos, (ScreenCoordsXY{ (size.width - status->width) / 2, size.height - status->height }));
    }
    previous->onMouseUp(1);
    EXPECT_EQ(state.editorStep, Editor::Step::objectSelection);
    manager->OpenWindow(WindowClass::topToolbar);
    Ui::ShortcutManager shortcuts(context->GetPlatformEnvironment());
    auto* toggle = shortcuts.getShortcut(Ui::ShortcutId::kInterfaceToggleToolbars);
    ASSERT_NE(toggle, nullptr);
    for (int cycle = 0; cycle < 3; cycle++)
    {
        toggle->action();
        EXPECT_EQ(manager->FindByClass(WindowClass::topToolbar), nullptr);
        EXPECT_EQ(manager->FindByClass(WindowClass::editorStepController), nullptr);
        EXPECT_EQ(manager->FindByClass(WindowClass::editorStatusLine), nullptr);
        toggle->action();
        EXPECT_NE(manager->FindByNumber(WindowClass::editorStepController, 0), nullptr);
        EXPECT_NE(manager->FindByNumber(WindowClass::editorStepController, 1), nullptr);
        EXPECT_NE(manager->FindByClass(WindowClass::editorStatusLine), nullptr);
    }
}

TEST(WidgetStateTest, FinancesGraphTabsKeepTheirHeightWithEitherTitleSizeAndButtonSide)
{
    gOpenRCT2Headless = true;
    gOpenRCT2NoGraphics = true;
    auto env = CreatePlatformEnvironment();
    auto uiContext = Ui::CreateUiContext(*env);
    auto context = CreateContext(std::move(env), Audio::CreateDummyAudioContext(), std::move(uiContext));
    ASSERT_TRUE(context->Initialise());
    auto& interfaceConfig = Config::Get().interface;
    struct RestoreTitleOptions
    {
        bool enlarged = Config::Get().interface.enlargedUi;
        bool onLeft = Config::Get().interface.windowButtonsOnTheLeft;
        ~RestoreTitleOptions()
        {
            Config::Get().interface.enlargedUi = enlarged;
            Config::Get().interface.windowButtonsOnTheLeft = onLeft;
        }
    } restore;
    for (bool enlarged : { false, true })
    {
        for (bool onLeft : { false, true })
        {
            SCOPED_TRACE(enlarged);
            SCOPED_TRACE(onLeft);
            interfaceConfig.enlargedUi = enlarged;
            interfaceConfig.windowButtonsOnTheLeft = onLeft;
            auto* window = Ui::Windows::FinancesOpen();
            ASSERT_NE(window, nullptr);
            // Finances has frame/title/close/page widgets, followed by six tabs.
            constexpr WidgetIndex firstTab = 4;
            window->onMouseUp(firstTab + 1);
            const auto initialHeight = window->height;
            const auto initialWidth = window->width;
            const auto titleHeight = enlarged ? kTitleHeightLarge : kTitleHeightNormal;
            const auto closeSize = enlarged ? kCloseButtonSizeTouch : kCloseButtonSize;
            for (int repeat = 0; repeat < 20; repeat++)
            {
                for (int page = 1; page <= 3; page++)
                {
                    window->onMouseUp(firstTab + page);
                    window->resizeFrame();
                    window->onPrepareDraw();
                    EXPECT_EQ(window->height, initialHeight);
                    EXPECT_EQ(window->width, initialWidth);
                    EXPECT_EQ(window->getTitleBarCurrentHeight(), titleHeight);
                    EXPECT_EQ(window->getTitleBarDiffTarget(), 0);
                    EXPECT_EQ(window->widgets[2].width(), closeSize.width);
                    EXPECT_EQ(window->widgets[2].height(), closeSize.height);
                    EXPECT_STREQ(window->widgets[2].string, enlarged ? u8"{BLACK}❌" : u8"{BLACK}✕");
                    EXPECT_EQ(window->widgets[2].left, onLeft ? 2 : window->width - 2 - closeSize.width);
                }
            }
            Ui::GetWindowManager()->CloseByClass(WindowClass::finances);
        }
    }
}

TEST(WidgetStateTest, GuestPickupRefreshesWithoutResizeAcrossPlatformAndRideStates)
{
    gOpenRCT2Headless = true;
    gOpenRCT2NoGraphics = true;
    auto env = CreatePlatformEnvironment();
    auto uiContext = Ui::CreateUiContext(*env);
    auto context = CreateContext(std::move(env), Audio::CreateDummyAudioContext(), std::move(uiContext));
    ASSERT_TRUE(context->Initialise());
    auto* guest = getGameState().entities.createEntity<Guest>();
    ASSERT_NE(guest, nullptr);
    guest->state = PeepState::picked;
    auto* window = Ui::Windows::GuestOpen(guest);
    ASSERT_NE(window, nullptr);
    const auto width = window->width;
    const auto height = window->height;
    for (const auto state : { PeepState::walking, PeepState::queuingFront, PeepState::walking, PeepState::onRide,
                              PeepState::leavingRide, PeepState::queuing })
    {
        SCOPED_TRACE(static_cast<int>(state));
        guest->state = state;
        window->onPrepareDraw();
        const bool allowed = state == PeepState::walking || state == PeepState::queuing;
        EXPECT_EQ(window->widgets[WC_PEEP__WIDX_PICKUP].flags.has(WidgetFlag::isDisabled), !allowed);
        EXPECT_EQ(window->width, width);
        EXPECT_EQ(window->height, height);
    }
    guest->state = PeepState::enteringRide;
    for (const auto subState :
         { PeepRideSubState::inEntrance, PeepRideSubState::approachPlatformSlot, PeepRideSubState::waitingOnPlatform })
    {
        guest->rideSubState = subState;
        window->onPrepareDraw();
        EXPECT_TRUE(window->widgets[WC_PEEP__WIDX_PICKUP].flags.has(WidgetFlag::isDisabled));
    }
    guest->state = PeepState::walking;
    window->onPrepareDraw();
    EXPECT_FALSE(window->widgets[WC_PEEP__WIDX_PICKUP].flags.has(WidgetFlag::isDisabled));
}

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
