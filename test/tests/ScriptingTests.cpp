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
#include <openrct2/entity/EntityRegistry.h>
#include <openrct2/ride/Ride.h>
#include <openrct2/ride/RideData.h>
#include <openrct2/ride/Vehicle.h>
#include <openrct2/actions/general/MapChangeSizeAction.h>
#include <openrct2/scripting/ScriptEngine.h>
#include <openrct2/world/Map.h>
#include <openrct2/world/MapTopology.h>
#include <openrct2/world/TileInspector.h>
#include <openrct2/world/tile_element/BannerElement.h>
#include <openrct2/world/tile_element/EntranceElement.h>
#include <openrct2/world/tile_element/PathElement.h>
#include <openrct2/world/tile_element/SurfaceElement.h>
#include <quickjs.h>

#if defined(ENABLE_SCRIPTING) && defined(OPENRCT2_TEST_UI_BINDINGS)
    #include "TestData.h"
    #include <openrct2-ui/UiContext.h>
    #include <openrct2-ui/scripting/UiExtensions.h>
    #include <openrct2/PlatformEnvironment.h>
    #include <openrct2/audio/AudioContext.h>
    #include <openrct2/core/File.h>
    #include <openrct2/core/Path.hpp>
    #include <openrct2/interface/Widget.h>
    #include <openrct2/interface/WindowBase.h>
    #include <openrct2/ui/UiContext.h>
    #include <openrct2/ui/WindowManager.h>
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

    #ifndef DISABLE_NETWORK
        #include <openrct2/scripting/bindings/network/ScSocket.hpp>

TEST_F(ScriptingTests, StoppingPluginCloseHandlersCannotScheduleTimersOrReconnect)
{
    struct ClosingSocket : SocketDataBase
    {
        bool ranWhileStopping{};
        bool checksPassed{};

        void Update() override {}
        void Dispose() override
        {
            if (_disposed)
                return;
            _disposed = true;
            ranWhileStopping = _plugin->IsStopping() && _plugin->HasStarted();
            _eventList.Raise(SocketData::EVENT_CLOSE, _plugin, { JS_FALSE }, false);
            auto* js = _plugin->GetContext();
            const char* check = "runningTimer !== 0 && closeCalls === 1 && stoppedTimeout === 0 && stoppedInterval === 0 && sameSocket";
            auto result = JS_Eval(js, check, strlen(check), "shutdown-check", JS_EVAL_TYPE_GLOBAL);
            checksPassed = !JS_IsException(result) && JS_ToBool(js, result) == 1;
            JS_FreeValue(js, result);
            _eventList.RemoveAllListeners();
        }
    };

    // Even without the stopping guard, this host must fail whitelist validation rather than open a real connection.
    ASSERT_FALSE(IsOnWhiteList("shutdown-regression.invalid"));
    auto& engine = static_cast<ScriptEngine&>(_context->GetScriptEngine());
    engine.AddNetworkPlugin(R"(
        globalThis.closeCalls = 0;
        function onShutdownClose() {
            closeCalls++;
            globalThis.stoppedTimeout = context.setTimeout(function(){ throw new Error('late timeout'); }, 1);
            globalThis.stoppedInterval = context.setInterval(function(){ throw new Error('late interval'); }, 1);
            const socket = network.createSocket();
            globalThis.sameSocket = socket.connect(1, 'shutdown-regression.invalid') === socket;
        }
        registerPlugin({name:'test-shutdown-resources', version:'1', authors:['openrct2-test'],
            type:'remote', licence:'MIT', minApiVersion:122, targetApiVersion:122,
            main:function(){ globalThis.runningTimer = context.setTimeout(function(){}, 100000); }});
    )");
    engine.LoadTransientPlugins();
    engine.Tick();
    auto plugin = std::find_if(engine.GetPlugins().begin(), engine.GetPlugins().end(), [](const auto& candidate) {
        return candidate->GetMetadata().Name == "test-shutdown-resources";
    });
    ASSERT_NE(plugin, engine.GetPlugins().end());
    ASSERT_TRUE((*plugin)->HasStarted());
    auto closing = std::make_shared<ClosingSocket>();
    closing->_plugin = *plugin;
    auto* js = (*plugin)->GetContext();
    auto global = JS_GetGlobalObject(js);
    auto callback = JS_GetPropertyStr(js, global, "onShutdownClose");
    ASSERT_TRUE(JS_IsFunction(js, callback));
    closing->_eventList.AddListener(SocketData::EVENT_CLOSE, JSCallback(js, callback));
    JS_FreeValue(js, callback);
    JS_FreeValue(js, global);
    engine.AddSocket(closing);
    engine.RemoveNetworkPlugins();
    EXPECT_TRUE(closing->ranWhileStopping);
    EXPECT_TRUE(closing->checksPassed);
    EXPECT_TRUE(closing->_disposed);
    engine.Tick();
}
    #endif

class PathNavigatorScriptingTests : public ScriptingTests
{
protected:
    JSContext* _js{};

    void SetUp() override
    {
        ScriptingTests::SetUp();
        MapInit({ 16, 16 });
        auto& engine = static_cast<ScriptEngine&>(_context->GetScriptEngine());
        engine.AddNetworkPlugin(R"(
            registerPlugin({
                name: 'test-path-navigator', version: '1', authors: ['openrct2-test'],
                type: 'remote', licence: 'MIT', minApiVersion: 120, targetApiVersion: 120,
                main: function () {}
            });
        )");
        engine.LoadTransientPlugins();
        engine.Tick();
        for (const auto& plugin : engine.GetPlugins())
        {
            if (plugin->GetMetadata().Name == "test-path-navigator")
            {
                ASSERT_TRUE(plugin->HasStarted());
                _js = plugin->GetContext();
            }
        }
        ASSERT_NE(_js, nullptr);
    }

    void Check(const char* code)
    {
        SCOPED_TRACE(code);
        auto result = JS_Eval(_js, code, strlen(code), "path-navigator-test", JS_EVAL_TYPE_GLOBAL);
        if (JS_IsException(result))
        {
            auto exception = JS_GetException(_js);
            const char* message = JS_ToCString(_js, exception);
            ADD_FAILURE() << (message == nullptr ? "JS exception" : message);
            JS_FreeCString(_js, message);
            JS_FreeValue(_js, exception);
        }
        else
        {
            EXPECT_EQ(JS_ToBool(_js, result), 1);
        }
        JS_FreeValue(_js, result);
    }

    static TileElement Path(uint8_t height = 14, uint8_t edges = 15)
    {
        TileElement element{};
        element.clearAs(TileElementType::path);
        element.baseHeight = height;
        element.clearanceHeight = height + 4;
        element.asPath()->setEdges(edges);
        return element;
    }

    void SetTile(const TileCoordsXY& tile, std::initializer_list<TileElement> paths)
    {
        std::vector<TileElement> elements{ *reinterpret_cast<TileElement*>(MapGetSurfaceElementAt(tile)) };
        elements.insert(elements.end(), paths);
        ASSERT_EQ(ReplaceTileElementsAt(tile, std::move(elements)), TileMutationStatus::ok);
    }

    void Capture()
    {
        Check("globalThis.nav = map.getPathNavigator({x:64,y:64},1); globalThis.saved = nav.current; saved !== null");
    }

    void CheckInvalid()
    {
        Check(R"(
            nav.current === null && nav.edges === null && nav.permittedEdges === null &&
            nav.getConnectedPaths() === null && nav.moveTo(0) === false &&
            saved.isSloped === null && saved.isWide === null && saved.isQueue === null &&
            saved.ride === null && saved.station === null && saved.slopeDirection === null &&
            saved.position.x === 64 && saved.position.y === 64 && saved.position.z === 112 &&
            saved.elementIndex === 1 && saved.direction === null
        )");
    }
};

TEST_F(PathNavigatorScriptingTests, PhysicalOptionsAndWorldCoordinatesDoNotChangeGuestTopology)
{
    SetTile({ 2, 2 }, { Path() });
    SetTile({ 1, 2 }, { Path(14, 0) }); // Upstream does not require a reciprocal neighbor edge.
    auto queue = Path();
    queue.asPath()->setIsQueue(true);
    queue.asPath()->setRideIndex(RideId::FromUnderlying(7));
    queue.asPath()->setStationIndex(StationIndex::FromUnderlying(2));
    SetTile({ 2, 3 }, { queue });
    auto wide = Path();
    wide.asPath()->setWide(true);
    SetTile({ 3, 2 }, { wide });
    auto ghost = Path();
    ghost.setGhost(true);
    SetTile({ 2, 1 }, { ghost });
    const auto topology = MapTopology::GetEpoch();
    const auto connectivity = MapTopology::GetPathConnectivityEpoch();
    Check(R"(
        (() => {
            for (let mask=0; mask<16; mask++) {
                const options = {includeQueues:!!(mask&1),includeWidePaths:!!(mask&2),
                    includeGhosts:!!(mask&4),respectBanners:!!(mask&8)};
                const nav = map.getPathNavigator({x:64,y:64,z:112},options);
                const expected = [0];
                if (options.includeQueues) expected.push(1);
                if (options.includeWidePaths) expected.push(2);
                if (options.includeGhosts) expected.push(3);
                const connections = nav.getConnectedPaths();
                if (JSON.stringify(connections.map(c=>c.direction)) !== JSON.stringify(expected)) return false;
                for (let d=0; d<4; d++) {
                    const moving = map.getPathNavigator({x:64,y:64},1,options);
                    if (moving.moveTo(d) !== expected.includes(d)) return false;
                    if (expected.includes(d) && moving.current.direction !== d) return false;
                }
                const q = connections.find(c=>c.direction===1);
                if (q && (!q.isQueue || q.ride!==7 || q.station!==2)) return false;
            }
            const startQueue = map.getPathNavigator({x:64,y:96,z:112});
            const startWide = map.getPathNavigator({x:96,y:64,z:112});
            const indexedGhost = map.getPathNavigator({x:64,y:32},1);
            return startQueue.current.isQueue && startWide.current.isWide && indexedGhost.current===null &&
                map.getPathNavigator({x:64,y:32,z:112})===null &&
                map.getPathNavigator({x:2,y:2},1)===null &&
                map.getPathNavigator({x:-32,y:64},1)===null &&
                map.getPathNavigator({x:64,y:64},999)===null &&
                map.getPathNavigator({x:64,y:64},1).moveTo(4)===false;
        })()
    )");
    EXPECT_EQ(MapTopology::GetEpoch(), topology);
    EXPECT_EQ(MapTopology::GetPathConnectivityEpoch(), connectivity);
    Check("globalThis.nav=map.getPathNavigator({x:64,y:64},1); globalThis.neighbor=nav.getConnectedPaths()[0]; true");
    SetTile({ 1, 2 }, { Path() });
    Check("nav.current!==null && neighbor.isWide===null && nav.moveTo(0) && nav.current.elementIndex===1");
    SetTile({ 2, 2 }, { Path() });
    Check("nav.current!==null && nav.current.position.x===32");
}

TEST_F(PathNavigatorScriptingTests, SlopesBannersAndFirstMatchingDuplicateFollowUpstreamRules)
{
    for (Direction direction = 0; direction < 4; ++direction)
    {
        SCOPED_TRACE(direction);
        auto slope = Path(14, (1 << direction) | (1 << DirectionReverse(direction)));
        slope.asPath()->setSloped(true);
        slope.asPath()->setSlopeDirection(direction);
        SetTile({ 4, 4 }, { slope });
        SetTile(TileCoordsXY{ 4, 4 } + TileDirectionDelta[direction], { Path(16) });
        SetTile(TileCoordsXY{ 4, 4 } + TileDirectionDelta[DirectionReverse(direction)], { Path(14) });
        auto global = JS_GetGlobalObject(_js);
        JS_SetPropertyStr(_js, global, "uphill", JS_NewInt32(_js, direction));
        JS_FreeValue(_js, global);
        Check(R"(
            (() => {
                const nav=map.getPathNavigator({x:128,y:128,z:112});
                if (!nav.current.isSloped || nav.current.slopeDirection!==uphill) return false;
                const paths=nav.getConnectedPaths();
                if (paths.length!==2 || paths.find(c=>c.direction===uphill).position.z!==128) return false;
                if (!nav.moveTo(uphill) || nav.current.position.z!==128) return false;
                return nav.moveTo((uphill+2)%4) && nav.current.position.z===112 && nav.current.isSloped;
            })()
        )");
    }
    TileElement banner{};
    banner.clearAs(TileElementType::banner);
    banner.baseHeight = 16;
    banner.asBanner()->setAllowedEdges(3);
    auto ghostBanner = banner;
    ghostBanner.setGhost(true);
    ghostBanner.asBanner()->setAllowedEdges(5);
    SetTile({ 2, 2 }, { Path(), banner, ghostBanner });
    auto duplicate = Path();
    duplicate.asPath()->setWide(true);
    SetTile({ 1, 2 }, { Path(), duplicate });
    SetTile({ 2, 3 }, { Path() });
    SetTile({ 3, 2 }, { Path() });
    SetTile({ 2, 1 }, { Path() });
    Check(R"(
        (() => {
            const unrestricted=map.getPathNavigator({x:64,y:64},1);
            const restricted=map.getPathNavigator({x:64,y:64},1,{respectBanners:true,includeWidePaths:true});
            const paths=restricted.getConnectedPaths();
            return unrestricted.edges===15 && unrestricted.permittedEdges===15 &&
                restricted.edges===15 && restricted.permittedEdges===1 && paths.length===1 &&
                paths[0].direction===0 && paths[0].elementIndex===1 && !paths[0].isWide &&
                restricted.moveTo(1)===false && restricted.moveTo(0)===true;
        })()
    )");
}

TEST_F(PathNavigatorScriptingTests, StructuralEditsInvalidateReferencesWithoutAliasingDuplicatePaths)
{
    SetTile({ 2, 2 }, { Path(), Path() });
    Capture();
    ASSERT_EQ(EraseTileElement({ 2, 2 }, MapGetNthElementAt({ 64, 64 }, 1)).status, TileMutationStatus::ok);
    CheckInvalid(); // The identical second path now occupies the saved index.
    Capture();
    SetTile({ 2, 2 }, { Path() }); // Even an identical replacement is a new identity.
    CheckInvalid();
    Capture();
    const auto topology = MapTopology::GetEpoch();
    auto ghost = Path(10);
    ghost.setGhost(true);
    ASSERT_EQ(InsertTileElement({ 2, 2 }, ghost, TileMutationMode::deferred).status, TileMutationStatus::ok);
    EXPECT_EQ(MapTopology::GetEpoch(), topology);
    CheckInvalid();
    ASSERT_EQ(
        EraseTileElement({ 2, 2 }, MapGetNthElementAt({ 64, 64 }, 0), TileMutationMode::deferred).status,
        TileMutationStatus::ok);
    ghost.baseHeight = 14;
    ASSERT_EQ(InsertTileElement({ 2, 2 }, ghost, TileMutationMode::deferred).status, TileMutationStatus::ok);
    Capture();
    ASSERT_EQ(
        EraseTileElement({ 2, 2 }, MapGetNthElementAt({ 64, 64 }, 2), TileMutationMode::deferred).status,
        TileMutationStatus::ok);
    EXPECT_EQ(MapTopology::GetEpoch(), topology);
    CheckInvalid();
    SetTile({ 2, 2 }, { Path(), Path() });
    Capture();
    ASSERT_EQ(TileInspector::SwapElementsAt({ 64, 64 }, 1, 2, true).error, GameActions::Status::ok);
    CheckInvalid();
    Capture();
    SetTile({ 8, 8 }, { Path() });
    MapGetNthElementAt({ 64, 64 }, 1)->asPath()->setEdges(5);
    Check("nav.current!==null && nav.edges===5 && saved.isSloped===false");
    Check("map.getTile(2,2).elements[1].type='track'; map.getTile(2,2).elements[1].type='footpath'; true");
    CheckInvalid();
}

TEST_F(PathNavigatorScriptingTests, PreviewMapsRestoreLiveReferencesWhileReplacedMapsInvalidate)
{
    SetTile({ 2, 2 }, { Path() });
    Capture();
    Check("globalThis.liveNav=nav; globalThis.liveSaved=saved; true");
    std::array<TileElement, 2> temporary{ *MapGetNthElementAt({ 64, 64 }, 0), Path() };
    temporary[0].setLastForTile(false);
    temporary[1].setLastForTile(true);
    {
        ScopedTileIndexOverride override({ { { 2, 2 }, temporary.data() } });
        CheckInvalid();
        Capture();
    }
    CheckInvalid();
    Check("liveNav.current!==null && liveSaved.isSloped===false");
    Capture();
    StashMap();
    MapInit({ 16, 16 });
    SetTile({ 2, 2 }, { Path() });
    CheckInvalid();
    Capture();
    UnstashMap();
    CheckInvalid();
    Check("liveNav.current!==null && liveSaved.isSloped===false");
    Capture();
    GameActions::MapChangeSizeAction expand({ 18, 18 });
    EXPECT_EQ(expand.Execute(getGameState(), getGameState().park).error, GameActions::Status::ok);
    Check("nav.current!==null && saved.isSloped===false");
    SetTile({ 16, 2 }, { Path() });
    Check("globalThis.edge=map.getPathNavigator({x:512,y:64},1); globalThis.edgeSaved=edge.current; edgeSaved!==null");
    GameActions::MapChangeSizeAction shrink({ 16, 16 });
    EXPECT_EQ(shrink.Execute(getGameState(), getGameState().park).error, GameActions::Status::ok);
    Check("nav.current!==null && saved.isSloped===false && edge.current===null && edgeSaved.isSloped===null");
    GameActions::MapChangeSizeAction shift({ 18, 18 }, { 1, 0 });
    EXPECT_EQ(shift.Execute(getGameState(), getGameState().park).error, GameActions::Status::ok);
    CheckInvalid();
}

TEST_F(PathNavigatorScriptingTests, ConstructionPreviewRestoresIdentityAcrossNestedTileOverrides)
{
    MapInit({ 150, 150 });
    // DrawTrackPieceHelper substitutes the tile at world (4096,4096) and its four neighbours solely for painting.
    const std::array<TileCoordsXY, 5> tiles{ TileCoordsXY{ 128, 128 }, { 127, 128 }, { 129, 128 }, { 128, 127 }, { 128, 129 } };
    for (const auto& tile : tiles)
        SetTile(tile, { Path() });
    Check(R"(
        globalThis.livePaths=[[128,128],[127,128],[129,128],[128,127],[128,129]].map(p=>
            map.getPathNavigator({x:p[0]*32,y:p[1]*32},1));
        globalThis.liveConnections=livePaths.map(n=>n.current);
        liveConnections.every(c=>c!==null)
    )");
    std::array<TileElement, 2> temporary{ *MapGetNthElementAt({ 4096, 4096 }, 0), Path() };
    temporary[0].setLastForTile(false);
    temporary[1].setLastForTile(true);
    for (int repaint = 0; repaint < 2; ++repaint)
    {
        {
            ScopedTileIndexOverride outer({
                { tiles[0], temporary.data() },
                { tiles[1], temporary.data() },
                { tiles[2], temporary.data() },
                { tiles[3], temporary.data() },
                { tiles[4], temporary.data() },
            });
            Check("livePaths.every(n=>n.current===null) && liveConnections.every(c=>c.isSloped===null)");
            if (repaint != 0)
                Check("outerNav.current===null && innerNav.current===null");
            Check("globalThis.outerNav=map.getPathNavigator({x:4096,y:4096},1); globalThis.outerConnection=outerNav.current; "
                  "true");
            {
                ScopedTileIndexOverride inner({ { tiles[0], temporary.data() } });
                Check("outerNav.current===null && outerConnection.isSloped===null");
                Check("globalThis.innerNav=map.getPathNavigator({x:4096,y:4096},1); "
                      "globalThis.innerConnection=innerNav.current; true");
            }
            Check("outerNav.current!==null && outerConnection.isSloped===false && innerNav.current===null && "
                  "innerConnection.isSloped===null");
        }
        Check(R"(
            livePaths.every(n=>n.current!==null) && liveConnections.every(c=>c.isSloped===false) &&
            outerNav.current===null && outerConnection.isSloped===null && innerNav.current===null
        )");
    }
    // Genuine mutation remains permanent even after later preview scopes.
    SetTile(tiles[0], { Path() });
    {
        ScopedTileIndexOverride preview({ { tiles[0], temporary.data() } });
    }
    Check("livePaths[0].current===null && liveConnections[0].isSloped===null && livePaths.slice(1).every(n=>n.current!==null)");
}

TEST_F(ScriptingTests, ObjectWrappersShareTypedPrototypesAndRetainInheritedGetters)
{
    auto& engine = static_cast<ScriptEngine&>(_context->GetScriptEngine());
    engine.AddNetworkPlugin(R"(
        registerPlugin({name:'test-object-prototypes', version:'1', authors:['openrct2-test'],
            type:'remote', licence:'MIT', minApiVersion:122, targetApiVersion:122, main:function(){}});
    )");
    engine.LoadTransientPlugins();
    engine.Tick();
    auto* js = engine.GetContext();
    ASSERT_NE(js, nullptr);
    const char* code = R"(
        (() => {
            const cases = [
                ['rct2.ride.spboat', 'description', v => typeof v === 'string'],
                ['rct2.scenery_small.tl0', 'height', v => typeof v === 'number'],
                ['rct2.scenery_large.badrack', 'tiles', v => Array.isArray(v) && v.length > 0],
                ['rct2.scenery_wall.wcw1', 'sceneryGroups', v => Array.isArray(v)],
                ['rct2.footpath_surface.tarmac', 'flags', v => typeof v === 'number'],
                ['rct2.scenery_group.scgclass', 'items', v => Array.isArray(v) && v.length > 0]
            ];
            globalThis.retainedObjectWrappers = [];
            for (const [id, property, validate] of cases) {
                const first = objectManager.load(id);
                if (!first) throw new Error('Could not load ' + id);
                const second = objectManager.getObject(first.type, first.index);
                if (first === second || Object.getPrototypeOf(first) !== Object.getPrototypeOf(second))
                    throw new Error('Prototype not shared: ' + id);
                if (first.identifier !== id || second.identifier !== id || !validate(first[property]))
                    throw new Error('Inherited getter failed: ' + id);
                if (Object.prototype.hasOwnProperty.call(first, property) || Object.keys(first).includes(property))
                    throw new Error('Derived getter still an own property: ' + id);
                if (Object.getOwnPropertyNames(first).includes(property))
                    throw new Error('Derived property is still stored on instance: ' + id);
                const descriptor = Object.getOwnPropertyDescriptor(Object.getPrototypeOf(first), property);
                if (!descriptor || typeof descriptor.get !== 'function' || descriptor.enumerable || !descriptor.configurable)
                    throw new Error('Getter descriptor changed: ' + id);
                const enumerable = [];
                for (const key in first) enumerable.push(key);
                if (enumerable.includes(property) || enumerable.includes('identifier'))
                    throw new Error('Non-enumerable getters became enumerable: ' + id);
                for (let i = 0; i < 20; i++) {
                    const copy = objectManager.getObject(first.type, first.index);
                    if (!validate(copy[property])) throw new Error('Repeated wrapper failed: ' + id);
                }
                retainedObjectWrappers.push(first);
            }
            return true;
        })()
    )";
    auto result = JS_Eval(js, code, strlen(code), "object-prototype-test", JS_EVAL_TYPE_GLOBAL);
    if (JS_IsException(result))
    {
        auto exception = JS_GetException(js);
        const char* message = JS_ToCString(js, exception);
        ADD_FAILURE() << (message == nullptr ? "JS exception" : message);
        JS_FreeCString(js, message);
        JS_FreeValue(js, exception);
    }
    else
    {
        EXPECT_EQ(JS_ToBool(js, result), 1);
    }
    JS_FreeValue(js, result);
    JS_RunGC(JS_GetRuntime(js));
    const char* afterGc = "retainedObjectWrappers.length === 6 && retainedObjectWrappers.every(o => o.identifier.length > 0)";
    result = JS_Eval(js, afterGc, strlen(afterGc), "object-prototype-gc-test", JS_EVAL_TYPE_GLOBAL);
    EXPECT_FALSE(JS_IsException(result));
    EXPECT_EQ(JS_ToBool(js, result), 1);
    JS_FreeValue(js, result);
}

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
TEST_F(ScriptingTests, ExplicitImageButtonBordersRetainImageAndVisibilityBindings)
{
    _context.reset();
    auto env = CreatePlatformEnvironment();
    auto uiContext = Ui::CreateUiContext(*env);
    _context = CreateContext(std::move(env), Audio::CreateDummyAudioContext(), std::move(uiContext));
    ASSERT_TRUE(_context->Initialise());
    auto& engine = static_cast<ScriptEngine&>(_context->GetScriptEngine());
    engine.AddNetworkPlugin(R"(
        registerPlugin({
            name:'test-button-border',version:'1',authors:['openrct2-test'],type:'remote',licence:'MIT',
            minApiVersion:120,targetApiVersion:120,
            main:function() {
                const window=ui.openWindow({classification:'test-button-border',title:'Border test',width:200,height:100,
                    widgets:[
                        {type:'button',name:'implicit',x:10,y:30,width:20,height:20,image:100},
                        {type:'button',name:'bordered',x:40,y:30,width:20,height:20,image:101,border:true},
                        {type:'button',name:'borderless',x:70,y:30,width:20,height:20,image:102,border:false},
                        {type:'button',name:'text',x:100,y:30,width:60,height:20,text:'Text',border:false}
                    ]});
                globalThis.exerciseBorder=function() {
                    const button=window.findWidget('borderless');
                    if (button.border || button.image!==102 || !button.isVisible) return false;
                    button.isVisible=false;
                    button.border=true;
                    if (!button.border || button.isVisible) return false;
                    button.border=false;
                    button.image=103;
                    if (button.border || button.image!==103 || button.isVisible) return false;
                    button.isVisible=true;
                    button.isPressed=true;
                    button.isDisabled=true;
                    return button.isVisible && button.isPressed && button.isDisabled && button.text==='';
                };
            }
        });
    )");
    engine.LoadTransientPlugins();
    engine.Tick();
    const auto& plugins = engine.GetPlugins();
    auto plugin = std::find_if(
        plugins.begin(), plugins.end(), [](const auto& item) { return item->GetMetadata().Name == "test-button-border"; });
    ASSERT_NE(plugin, plugins.end());
    ASSERT_TRUE((*plugin)->HasStarted());
    auto* window = Ui::GetWindowManager()->FindByClass(WindowClass::custom);
    ASSERT_NE(window, nullptr);
    const auto findImage = [&](uint32_t image) -> const Widget* {
        for (const auto& widget : window->widgets)
        {
            if (widget.image.GetIndex() == image)
                return &widget;
        }
        return nullptr;
    };
    ASSERT_NE(findImage(100), nullptr);
    ASSERT_NE(findImage(101), nullptr);
    ASSERT_NE(findImage(102), nullptr);
    EXPECT_EQ(findImage(100)->type, WidgetType::flatBtn);
    EXPECT_EQ(findImage(101)->type, WidgetType::imgBtn);
    EXPECT_EQ(findImage(102)->type, WidgetType::hiddenButton);
    EXPECT_FALSE(findImage(102)->flags.has(WidgetFlag::isHidden));
    auto* ctx = (*plugin)->GetContext();
    auto global = JS_GetGlobalObject(ctx);
    auto function = JS_GetPropertyStr(ctx, global, "exerciseBorder");
    auto result = engine.ExecutePluginCall(*plugin, function, JS_UNDEFINED, {}, false, false, true);
    EXPECT_FALSE(JS_IsException(result));
    EXPECT_EQ(JS_ToBool(ctx, result), 1);
    ASSERT_NE(findImage(103), nullptr);
    EXPECT_EQ(findImage(103)->type, WidgetType::hiddenButton);
    EXPECT_FALSE(findImage(103)->flags.has(WidgetFlag::isHidden));
    EXPECT_TRUE(findImage(103)->flags.has(WidgetFlag::isPressed));
    EXPECT_TRUE(findImage(103)->flags.has(WidgetFlag::isDisabled));
    JS_FreeValue(ctx, result);
    JS_FreeValue(ctx, function);
    JS_FreeValue(ctx, global);
}

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


class VehicleSubpositionScriptingTests : public ScriptingTests
{
protected:
    JSContext* _js{};
    Vehicle* _vehicle{};

    void SetUp() override
    {
        ScriptingTests::SetUp();
        MapInit({ 16, 16 });
        _vehicle = getGameState().entities.createEntity<Vehicle>();
        ASSERT_NE(_vehicle, nullptr);
        _vehicle->ride = RideId::GetNull();
        _vehicle->TrackSubposition = VehicleTrackSubposition::standard;
        auto& engine = static_cast<ScriptEngine&>(_context->GetScriptEngine());
        engine.AddNetworkPlugin(R"(
            registerPlugin({name:'test-subposition',version:'1',authors:['openrct2-test'],
                type:'remote',licence:'MIT',minApiVersion:122,targetApiVersion:122,main:function(){}});
        )");
        engine.LoadTransientPlugins();
        engine.Tick();
        for (const auto& plugin : engine.GetPlugins())
        {
            if (plugin->GetMetadata().Name == "test-subposition")
            {
                ASSERT_TRUE(plugin->HasStarted());
                _js = plugin->GetContext();
            }
        }
        ASSERT_NE(_js, nullptr);
        Check("globalThis.vehicle = map.getEntity(" + std::to_string(_vehicle->id.ToUnderlying()) + "); vehicle !== null");
    }

    void Check(const std::string& code)
    {
        SCOPED_TRACE(code);
        auto result = JS_Eval(_js, code.c_str(), code.size(), "subposition-test", JS_EVAL_TYPE_GLOBAL);
        if (JS_IsException(result))
        {
            auto exception = JS_GetException(_js);
            const auto* message = JS_ToCString(_js, exception);
            ADD_FAILURE() << (message ? message : "JS exception");
            JS_FreeCString(_js, message);
            JS_FreeValue(_js, exception);
        }
        else
            EXPECT_EQ(JS_ToBool(_js, result), 1);
        JS_FreeValue(_js, result);
    }
};

TEST_F(VehicleSubpositionScriptingTests, BoundsConversionAndMissingMoveInfoPublishOwnedState)
{
    auto& entities = getGameState().entities;
    for (uint32_t value = 0; value < static_cast<uint32_t>(VehicleTrackSubposition::count); value++)
    {
        (void)entities.ConsumeEntityVisualChanges();
        Check("vehicle.subposition = " + std::to_string(value) + "; vehicle.subposition === " + std::to_string(value));
        EXPECT_EQ(static_cast<uint32_t>(_vehicle->TrackSubposition), value);
        const auto batch = entities.ConsumeEntityVisualChanges();
        ASSERT_EQ(batch.changes.size(), 1u);
        const auto& change = batch.changes.front();
        EXPECT_EQ(change.handle.id, _vehicle->id);
        EXPECT_EQ(change.dirty, EntityVisualDirty::full);
        ASSERT_EQ(change.payloadSize, sizeof(Vehicle));
        Vehicle captured{};
        std::memcpy(&captured, batch.payload.data() + change.payloadOffset, sizeof(captured));
        EXPECT_EQ(static_cast<uint32_t>(captured.TrackSubposition), value);
    }
    Check(R"((() => {
        const before = vehicle.subposition;
        for (const value of [-1, 17, 255, 4294967295]) {
            let caught = false;
            try { vehicle.subposition = value; } catch (e) { caught = e.name === 'RangeError'; }
            if (!caught || vehicle.subposition !== before) throw new Error('range check: ' + value);
        }
        for (const value of ['1', null, true, {}]) {
            let caught = false;
            try { vehicle.subposition = value; } catch (e) { caught = e.name === 'TypeError'; }
            if (!caught || vehicle.subposition !== before) throw new Error('type check: ' + value);
        }
        // Upstream uses numeric ToUint32 conversion before checking enum bounds.
        for (const [value, expected] of [[1.5,1],[4294967296,0],[NaN,0],[Infinity,0]]) {
            vehicle.subposition = value;
            if (vehicle.subposition !== expected) throw new Error('conversion: ' + value + ' -> ' + vehicle.subposition);
        }
        return true;
    })())");
    const auto id = _vehicle->id;
    entities.entityRemove(_vehicle);
    _vehicle = nullptr;
    Check("vehicle.subposition = 1; vehicle.subposition === 0");
    EXPECT_EQ(entities.tryGetEntity<Vehicle>(id), nullptr);
}

TEST_F(VehicleSubpositionScriptingTests, TrackChangePreservesSeatsAndAccruedDirectedSamples)
{
    auto* ride = RideAllocateAtIndex(RideId::FromUnderlying(0));
    ASSERT_NE(ride, nullptr);
    ride->type = RIDE_TYPE_GO_KARTS;
    _vehicle->ride = ride->id;
    _vehicle->TrackLocation = { 128, 128, 112 };
    _vehicle->SetTrackType(TrackElemType::flat);
    _vehicle->SetTrackDirection(2);
    _vehicle->track_progress = 0;
    _vehicle->TrackSubposition = VehicleTrackSubposition::goKartsLeftLane;
    _vehicle->UpdateTrackChange();
    const auto oldPosition = _vehicle->getLocation();
    _vehicle->num_peeps = 1;
    _vehicle->num_seats = 2;
    _vehicle->peep[0] = EntityId::FromUnderlying(123);
    _vehicle->velocity = 12345;
    _vehicle->remaining_distance = 678;
    ride->ratingAccumulator.ticks = 40;
    ride->ratingAccumulator.excitement = 123456;
    ride->activeRatingSamples.resize(1);
    ride->activeRatingSamples[0].originStation = StationIndex::FromUnderlying(0);
    ride->activeRatingSamples[0].destinationStation = StationIndex::FromUnderlying(1);
    ride->activeRatingSamples[0].ticks = 23;
    ride->activeRatingSamples[0].transportDistance = 9876;
    ride->recentRatingSampleCount = 1;
    ride->recentRatingSamples[0].ticks = 88;
    Check("vehicle.subposition = 6; vehicle.subposition === 6");
    EXPECT_NE(_vehicle->getLocation(), oldPosition);
    EXPECT_EQ(_vehicle->track_progress, 0);
    EXPECT_EQ(_vehicle->remaining_distance, 678);
    EXPECT_EQ(_vehicle->velocity, 12345);
    EXPECT_EQ(_vehicle->num_peeps, 1);
    EXPECT_EQ(_vehicle->num_seats, 2);
    EXPECT_EQ(_vehicle->peep[0], EntityId::FromUnderlying(123));
    EXPECT_EQ(ride->ratingAccumulator.ticks, 40u);
    EXPECT_EQ(ride->ratingAccumulator.excitement, 123456);
    ASSERT_EQ(ride->activeRatingSamples.size(), 1u);
    EXPECT_EQ(ride->activeRatingSamples[0].originStation, StationIndex::FromUnderlying(0));
    EXPECT_EQ(ride->activeRatingSamples[0].destinationStation, StationIndex::FromUnderlying(1));
    EXPECT_EQ(ride->activeRatingSamples[0].ticks, 23u);
    EXPECT_EQ(ride->activeRatingSamples[0].transportDistance, 9876);
    EXPECT_EQ(ride->recentRatingSampleCount, 1);
    EXPECT_EQ(ride->recentRatingSamples[0].ticks, 88u);
}

#endif
