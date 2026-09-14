/*****************************************************************************
 * Copyright (c) 2014-2026 OpenRCT2 developers
 *
 * For a complete list of all authors, please refer to contributors.md
 * Interested in contributing? Visit https://github.com/OpenRCT2/OpenRCT2
 *
 * OpenRCT2 is licensed under the GNU General Public License version 3.
 *****************************************************************************/

#include <algorithm>
#include <cstring>
#include <gtest/gtest.h>
#include <openrct2/Context.h>
#include <openrct2/GameState.h>
#include <openrct2/OpenRCT2.h>
#include <openrct2/actions/general/MapChangeSizeAction.h>
#include <openrct2/scripting/ScriptEngine.h>
#include <openrct2/world/Map.h>
#include <openrct2/world/MapTopology.h>
#include <quickjs.h>

using namespace OpenRCT2;
using namespace OpenRCT2::Scripting;

class ScriptingTests : public testing::Test
{
protected:
    void SetUp() override
    {
        gOpenRCT2Headless = true;
        gOpenRCT2NoGraphics = true;
        _context = CreateContext();
        _context->Initialise();
    }

    std::unique_ptr<IContext> _context;
};

#ifdef ENABLE_SCRIPTING

TEST_F(ScriptingTests, MultipleSubscribersToSameEventShouldNotCrash)
{
    auto& scriptEngine = static_cast<ScriptEngine&>(_context->GetScriptEngine());

    // Register a plugin that subscribes twice to the same event
    const char* pluginCode = R"(
        registerPlugin({
            name: 'test-plugin-multiple-subscribers',
            version: '1.0.0',
            authors: ['openrct2-test'],
            type: 'remote',
            licence: 'MIT',
            minApiVersion: 110, // deliberately the version before quickjs
            targetApiVersion: 110,
            main: function () {
                context.subscribe('interval.tick', function (e) {
                    // first subscriber
                });
                context.subscribe('interval.tick', function (e) {
                    // second subscriber
                });
            }
        });
    )";

    scriptEngine.AddNetworkPlugin(pluginCode);
    scriptEngine.LoadTransientPlugins();
    scriptEngine.Tick();

    auto& hookEngine = scriptEngine.GetHookEngine();

    // We need a JSValue to pass to Call.
    JSContext* ctx = scriptEngine.GetContext();
    JSValue arg = JS_NewObject(ctx);
    JS_SetPropertyStr(ctx, arg, "test", JS_NewInt32(ctx, 1));

    // This should NOT crash.
    hookEngine.Call(HookType::intervalTick, arg, false);
}

TEST_F(ScriptingTests, MapResizeHookObservesCompletedChangesAndAllowsStateUpdates)
{
    auto& scriptEngine = static_cast<ScriptEngine&>(_context->GetScriptEngine());
    MapInit({ 16, 16 });
    auto& state = getGameState();
    state.park.cash = 0;
    const char* pluginCode = R"(
        globalThis.resizeEvents = [];
        registerPlugin({
            name: 'test-map-resize', version: '1.0.0', authors: ['openrct2-test'],
            type: 'remote', licence: 'MIT', minApiVersion: 118, targetApiVersion: 118,
            main: function () {
                globalThis.resizeSubscription = context.subscribe('map.resize', function (e) {
                    resizeEvents.push({
                        targetSizeX: e.targetSizeX, targetSizeY: e.targetSizeY,
                        shiftX: e.shiftX, shiftY: e.shiftY,
                        actualX: map.size.x, actualY: map.size.y
                    });
                    park.cash += 1;
                });
            }
        });
    )";
    scriptEngine.AddNetworkPlugin(pluginCode);
    scriptEngine.LoadTransientPlugins();
    scriptEngine.Tick();
    const auto& plugins = scriptEngine.GetPlugins();
    const auto plugin = std::find_if(plugins.begin(), plugins.end(), [](const auto& candidate) {
        return candidate->GetMetadata().Name == "test-map-resize";
    });
    ASSERT_NE(plugin, plugins.end());
    ASSERT_TRUE((*plugin)->HasStarted());
    auto* ctx = (*plugin)->GetContext();
    auto global = JS_GetGlobalObject(ctx);
    auto events = JS_GetPropertyStr(ctx, global, "resizeEvents");
    auto eventCount = [&] { return AsOrDefault(ctx, events, "length", int32_t{}); };
    EXPECT_EQ(GetHookType("map.resize"), HookType::mapResize);
    ASSERT_TRUE(scriptEngine.GetHookEngine().HasSubscriptions(HookType::mapResize));

    const TileCoordsXY targets[] = { { 18, 20 }, { 16, 16 }, { 16, 16 }, { 16, 16 } };
    const TileCoordsXY shifts[] = { { 1, 2 }, { -1, -2 }, { 1, 0 }, { 0, 0 } };
    for (int32_t i = 0; i < 4; ++i)
    {
        SCOPED_TRACE(i);
        GameActions::MapChangeSizeAction action(targets[i], shifts[i]);
        const auto previousSize = state.mapSize;
        const auto previousEpoch = MapTopology::GetEpoch();
        EXPECT_EQ(action.Query(state, state.park).error, GameActions::Status::ok);
        EXPECT_EQ(state.mapSize, previousSize);
        EXPECT_EQ(eventCount(), i);
        EXPECT_EQ(action.Execute(state, state.park).error, GameActions::Status::ok);
        EXPECT_EQ(state.mapSize, targets[i]);
        EXPECT_EQ(eventCount(), i + 1);
        EXPECT_EQ(state.park.cash, i + 1);
        if (targets[i] != previousSize)
            EXPECT_GT(MapTopology::GetEpoch(), previousEpoch);
        auto event = JS_GetPropertyUint32(ctx, events, i);
        EXPECT_EQ(AsOrDefault(ctx, event, "targetSizeX", -1), targets[i].x);
        EXPECT_EQ(AsOrDefault(ctx, event, "targetSizeY", -1), targets[i].y);
        EXPECT_EQ(AsOrDefault(ctx, event, "shiftX", -99), shifts[i].x);
        EXPECT_EQ(AsOrDefault(ctx, event, "shiftY", -99), shifts[i].y);
        EXPECT_EQ(AsOrDefault(ctx, event, "actualX", -1), targets[i].x);
        EXPECT_EQ(AsOrDefault(ctx, event, "actualY", -1), targets[i].y);
        JS_FreeValue(ctx, event);
    }
    GameActions::MapChangeSizeAction invalid({ 1, 1 });
    EXPECT_EQ(invalid.Query(state, state.park).error, GameActions::Status::invalidParameters);
    EXPECT_EQ(eventCount(), 4);
    const char* dispose = "resizeSubscription.dispose()";
    auto result = JS_Eval(ctx, dispose, strlen(dispose), "test", JS_EVAL_TYPE_GLOBAL);
    EXPECT_FALSE(JS_IsException(result));
    JS_FreeValue(ctx, result);
    EXPECT_FALSE(scriptEngine.GetHookEngine().HasSubscriptions(HookType::mapResize));
    GameActions::MapChangeSizeAction unsubscribed({ 17, 17 });
    EXPECT_EQ(unsubscribed.Execute(state, state.park).error, GameActions::Status::ok);
    EXPECT_EQ(eventCount(), 4);
    EXPECT_EQ(state.park.cash, 4);
    JS_FreeValue(ctx, events);
    JS_FreeValue(ctx, global);
}

#endif
