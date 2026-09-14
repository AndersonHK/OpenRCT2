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
#include <openrct2/world/tile_element/EntranceElement.h>
#include <openrct2/world/tile_element/SurfaceElement.h>
#include <quickjs.h>

#if defined(ENABLE_SCRIPTING) && defined(OPENRCT2_TEST_UI_BINDINGS)
    #include "TestData.h"
    #include <openrct2-ui/scripting/UiExtensions.h>
    #include <openrct2/core/File.h>
    #include <openrct2/core/Path.hpp>
#endif

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

    #ifdef OPENRCT2_TEST_UI_BINDINGS
TEST_F(ScriptingTests, CustomImageErrorsAreCatchablePreserveTheImageAndReleaseBuffers)
{
    struct RestoreImageContext
    {
        std::unique_ptr<IContext>& context;
        bool noGraphics;
        ~RestoreImageContext()
        {
            context.reset();
            gOpenRCT2NoGraphics = noGraphics;
        }
    } restore{ _context, gOpenRCT2NoGraphics };
    // Custom image allocation requires sprite storage; no display or drawing engine is needed.
    gOpenRCT2NoGraphics = false;
    auto& scriptEngine = static_cast<ScriptEngine&>(_context->GetScriptEngine());
    UiScriptExtensions::Extend(scriptEngine);
    const char* pluginCode = R"(
        registerPlugin({
            name: 'test-image-errors', version: '1.0.0', authors: ['openrct2-test'],
            type: 'remote', licence: 'MIT', minApiVersion: 119, targetApiVersion: 119,
            main: function () {
                const manager = ui.imageManager;
                const range = manager.allocate(1);
                globalThis.uploadImage = function (data, palette) {
                    let caught = false;
                    let message = '';
                    try {
                        manager.setPixelData(range.start, { type: 'png', palette: palette, data: new Uint8Array(data) });
                    } catch (e) {
                        caught = true;
                        message = String(e);
                    }
                    const info = manager.getImageInfo(range.start);
                    return { caught: caught, message: message, width: info.width, height: info.height };
                };
            }
        });
    )";
    scriptEngine.AddNetworkPlugin(pluginCode);
    scriptEngine.LoadTransientPlugins();
    scriptEngine.Tick();
    const auto& plugins = scriptEngine.GetPlugins();
    const auto plugin = std::find_if(plugins.begin(), plugins.end(), [](const auto& candidate) {
        return candidate->GetMetadata().Name == "test-image-errors";
    });
    ASSERT_NE(plugin, plugins.end());
    ASSERT_TRUE((*plugin)->HasStarted());
    auto* ctx = (*plugin)->GetContext();
    auto global = JS_GetGlobalObject(ctx);
    auto upload = JS_GetPropertyStr(ctx, global, "uploadImage");
    ASSERT_TRUE(JS_IsFunction(ctx, upload));
    const auto run = [&](const std::vector<uint8_t>& png, const char* palette, bool shouldFail) {
        auto data = JS_NewArray(ctx);
        for (uint32_t i = 0; i < png.size(); ++i)
            JS_SetPropertyUint32(ctx, data, i, JS_NewInt32(ctx, png[i]));
        auto result = scriptEngine.ExecutePluginCall(
            *plugin, upload, JS_UNDEFINED, { data, JS_NewString(ctx, palette) }, false, false, true);
        EXPECT_FALSE(JS_IsException(result));
        EXPECT_EQ(AsOrDefault(ctx, result, "caught", false), shouldFail);
        EXPECT_EQ(AsOrDefault(ctx, result, "width", -1), 1);
        EXPECT_EQ(AsOrDefault(ctx, result, "height", -1), 1);
        if (shouldFail)
            EXPECT_FALSE(JSToStdString(ctx, result, "message").empty());
        JS_FreeValue(ctx, result);
    };
    const auto read = [](const char* name) {
        return File::ReadAllBytes(Path::Combine(TestData::GetBasePath(), "images", name));
    };
    const auto valid = read("rgba-1x1.png");
    const auto tooWide = read("rgba-301x1.png");
    run(valid, "closest", false);
    run(tooWide, "closest", true);
    run(read("rgba-1x301.png"), "closest", true);
    run(valid, "keep", true);
    run({ 0, 1, 2, 3, 4, 5, 6, 7 }, "closest", true);
    auto* runtime = JS_GetRuntime(ctx);
    JS_RunGC(runtime);
    JSMemoryUsage before{}, after{};
    JS_ComputeMemoryUsage(runtime, &before);
    for (int i = 0; i < 32; ++i)
        run(tooWide, "closest", true);
    JS_RunGC(runtime);
    JS_ComputeMemoryUsage(runtime, &after);
    EXPECT_EQ(after.obj_count, before.obj_count);
    run(valid, "closest", false);
    JS_FreeValue(ctx, upload);
    JS_FreeValue(ctx, global);
}
    #endif

TEST_F(ScriptingTests, OwnershipUsesApi119FlagsAndPreservesPackedFences)
{
    auto& scriptEngine = static_cast<ScriptEngine&>(_context->GetScriptEngine());
    MapInit({ 16, 16 });
    auto* surface = MapGetSurfaceElementAt(CoordsXY{ 64, 64 });
    ASSERT_NE(surface, nullptr);
    surface->setParkFences(5);
    const char* pluginCode = R"(
        registerPlugin({
            name: 'test-ownership-api119', version: '1.0.0', authors: ['openrct2-test'],
            type: 'remote', licence: 'MIT', minApiVersion: 119, targetApiVersion: 119,
            main: function () {
                globalThis.testSurface = map.getTile(2, 2).elements.find(e => e.type === 'surface');
            }
        });
    )";
    scriptEngine.AddNetworkPlugin(pluginCode);
    scriptEngine.LoadTransientPlugins();
    scriptEngine.Tick();
    const auto& plugins = scriptEngine.GetPlugins();
    const auto plugin = std::find_if(plugins.begin(), plugins.end(), [](const auto& candidate) {
        return candidate->GetMetadata().Name == "test-ownership-api119";
    });
    ASSERT_NE(plugin, plugins.end());
    ASSERT_TRUE((*plugin)->HasStarted());
    auto* ctx = (*plugin)->GetContext();
    auto global = JS_GetGlobalObject(ctx);
    auto element = JS_GetPropertyStr(ctx, global, "testSurface");
    ASSERT_TRUE(JS_IsObject(element));
    const auto verify = [&](int64_t input, uint32_t expected) {
        SCOPED_TRACE(input);
        EXPECT_EQ(JS_SetPropertyStr(ctx, element, "ownership", JS_NewInt64(ctx, input)), 1);
        auto value = JS_GetPropertyStr(ctx, element, "ownership");
        uint32_t actual{};
        EXPECT_EQ(JS_ToUint32(ctx, &actual, value), 0);
        EXPECT_EQ(actual, expected);
        EXPECT_EQ(surface->getOwnership().holder, expected);
        EXPECT_EQ(surface->getParkFences(), 5);
        EXPECT_EQ(reinterpret_cast<const uint8_t*>(surface)[8], (expected << 4) | 5);
        JS_FreeValue(ctx, value);
        auto owned = JS_GetPropertyStr(ctx, element, "hasOwnership");
        auto rights = JS_GetPropertyStr(ctx, element, "hasConstructionRights");
        EXPECT_EQ(JS_ToBool(ctx, owned), (expected & 2) != 0);
        EXPECT_EQ(JS_ToBool(ctx, rights), (expected & 3) != 0);
        JS_FreeValue(ctx, owned);
        JS_FreeValue(ctx, rights);
    };
    for (int64_t input = 0; input < 256; ++input)
        verify(input, static_cast<uint32_t>(input) & 15);
    verify(256, 0);
    verify(257, 1);
    verify(-1, 15);
    verify(4294967296LL, 0);
    JS_FreeValue(ctx, element);
    JS_FreeValue(ctx, global);
}

TEST_F(ScriptingTests, EntranceObjectAndSequenceWritesClampAndInvalidateTopology)
{
    auto& scriptEngine = static_cast<ScriptEngine&>(_context->GetScriptEngine());
    MapInit({ 16, 16 });
    const TileCoordsXY tile{ 2, 2 };
    auto* entrance = InsertTileElement<EntranceElement>(
        { tile.ToCoordsXY(), 10 * kCoordsZStep }, 0, [](EntranceElement& element) {
            element.setEntranceType(EntranceType::rideEntrance);
            element.setSequenceIndex(ParkEntranceSequence::centre);
            element.setClearanceZ(14 * kCoordsZStep);
        });
    ASSERT_NE(entrance, nullptr);
    const char* pluginCode = R"(
        registerPlugin({
            name: 'test-entrance-object-bounds', version: '1.0.0', authors: ['openrct2-test'],
            type: 'remote', licence: 'MIT', minApiVersion: 118, targetApiVersion: 118,
            main: function () {
                globalThis.testEntrance = map.getTile(2, 2).elements.find(e => e.type === 'entrance');
            }
        });
    )";
    scriptEngine.AddNetworkPlugin(pluginCode);
    scriptEngine.LoadTransientPlugins();
    scriptEngine.Tick();
    const auto& plugins = scriptEngine.GetPlugins();
    const auto plugin = std::find_if(plugins.begin(), plugins.end(), [](const auto& candidate) {
        return candidate->GetMetadata().Name == "test-entrance-object-bounds";
    });
    ASSERT_NE(plugin, plugins.end());
    ASSERT_TRUE((*plugin)->HasStarted());
    auto* ctx = (*plugin)->GetContext();
    auto global = JS_GetGlobalObject(ctx);
    auto element = JS_GetPropertyStr(ctx, global, "testEntrance");
    ASSERT_TRUE(JS_IsObject(element));
    struct Case
    {
        int64_t input;
        uint32_t expected;
    };
    const Case cases[] = { { 0, 0 },   { 1, 1 },           { 2, 2 },   { 3, 2 },
                           { 255, 2 }, { 256, 2 },         { 257, 2 }, { 4294967295LL, 2 },
                           { -1, 2 },  { 4294967296LL, 0 } };
    for (const auto& test : cases)
    {
        SCOPED_TRACE(test.input);
        const auto generation = MapTopology::GetChunkGeneration(tile);
        EXPECT_EQ(JS_SetPropertyStr(ctx, element, "object", JS_NewInt64(ctx, test.input)), 1);
        auto value = JS_GetPropertyStr(ctx, element, "object");
        uint32_t actual{};
        EXPECT_EQ(JS_ToUint32(ctx, &actual, value), 0);
        EXPECT_EQ(actual, test.expected);
        EXPECT_EQ(static_cast<uint8_t>(entrance->getEntranceType()), test.expected);
        EXPECT_EQ(entrance->getDirections(), test.expected == 2 ? 5 : 4);
        EXPECT_GT(MapTopology::GetChunkGeneration(tile), generation);
        JS_FreeValue(ctx, value);
    }
    EXPECT_EQ(JS_SetPropertyStr(ctx, element, "object", JS_NewInt32(ctx, 2)), 1);
    const Case sequenceCases[] = { { 0, 0 },   { 1, 1 },   { 2, 2 },   { 3, 2 },   { 7, 2 },
                                   { 8, 2 },   { 15, 2 },  { 16, 2 },  { 255, 2 }, { 256, 0 },
                                   { 257, 1 }, { 258, 2 }, { 259, 2 }, { -1, 2 },  { 4294967296LL, 0 } };
    for (const auto& test : sequenceCases)
    {
        SCOPED_TRACE(test.input);
        const auto generation = MapTopology::GetChunkGeneration(tile);
        EXPECT_EQ(JS_SetPropertyStr(ctx, element, "sequence", JS_NewInt64(ctx, test.input)), 1);
        auto value = JS_GetPropertyStr(ctx, element, "sequence");
        uint32_t actual{};
        EXPECT_EQ(JS_ToUint32(ctx, &actual, value), 0);
        EXPECT_EQ(actual, test.expected);
        EXPECT_EQ(static_cast<uint8_t>(entrance->getSequenceIndex()), test.expected);
        EXPECT_EQ(entrance->getDirections(), test.expected == 0 ? 5 : 0);
        EXPECT_GT(MapTopology::GetChunkGeneration(tile), generation);
        JS_FreeValue(ctx, value);
    }
    JS_FreeValue(ctx, element);
    JS_FreeValue(ctx, global);
}

#endif
