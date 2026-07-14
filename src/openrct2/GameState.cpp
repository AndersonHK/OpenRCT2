/*****************************************************************************
 * Copyright (c) 2014-2026 OpenRCT2 developers
 *
 * For a complete list of all authors, please refer to contributors.md
 * Interested in contributing? Visit https://github.com/OpenRCT2/OpenRCT2
 *
 * OpenRCT2 is licensed under the GNU General Public License version 3.
 *****************************************************************************/

#include "GameState.h"

#include "Game.h"
#include "GameStateSnapshots.h"
#include "Input.h"
#include "OpenRCT2.h"
#include "ReplayManager.h"
#include "actions/GameActionRunner.h"
#include "config/Config.h"
#include "drawing/Drawing.h"
#include "entity/EntityTweener.h"
#include "entity/PatrolArea.h"
#include "interface/Screenshot.h"
#include "platform/Platform.h"
#include "peep/GuestPathfinding.h"
#include "profiling/Profiling.h"
#include "ride/Vehicle.h"
#include "scenario/Scenario.h"
#include "scenes/editor/EditorScene.h"
#include "scenes/title/TitleScene.h"
#include "scenes/title/TitleSequencePlayer.h"
#include "scripting/ScriptEngine.h"
#include "ui/UiContext.h"
#include "windows/Intent.h"
#include "world/Map.h"
#include "world/MapAnimation.h"
#include "world/Park.h"
#include "world/Scenery.h"

using namespace OpenRCT2::Scripting;

namespace OpenRCT2
{
    static auto _gameState = std::make_unique<GameState_t>();
    static bool _presentationAudioPending{};

    GameState_t& getGameState()
    {
        return *_gameState;
    }

    void swapGameState(std::unique_ptr<GameState_t>& otherState)
    {
        _gameState.swap(otherState);
    }

    /**
     * Initialises the map, park etc. basically all S6 data.
     */
    void gameStateInitAll(GameState_t& gameState, const TileCoordsXY& mapSize)
    {
        PROFILED_FUNCTION();

        gInMapInitCode = true;
        gameState.currentTicks = 0;
        _presentationAudioPending = false;

        MapInit(mapSize);
        Park::Initialise(gameState.park, gameState);
        FinanceInit();
        BannerInit(gameState);
        RideInitAll();
        gameState.entities.ResetAllEntities();
        UpdateConsolidatedPatrolAreas();
        ResetDate();
        Weather::reset();
        News::InitQueue(gameState);

        gInMapInitCode = false;

        gameState.nextGuestNumber = 1;

        ContextInit();

        auto sceneryIntent = Intent(INTENT_ACTION_SET_DEFAULT_SCENERY_CONFIG);
        ContextBroadcastIntent(&sceneryIntent);

        auto clipboardIntent = Intent(INTENT_ACTION_CLEAR_TILE_INSPECTOR_CLIPBOARD);
        ContextBroadcastIntent(&clipboardIntent);

        LoadPalette();

        CheatsReset();
        ClearRestrictedScenery();

#ifdef ENABLE_SCRIPTING
        auto& scriptEngine = GetContext()->GetScriptEngine();
        scriptEngine.ClearParkStorage();
#endif

        EntityTweener::Get().Reset();
    }

    /**
     * Runs one logical update per offline scheduler tick. Network sessions retain their
     * ordered catch-up loop because clients must follow the authoritative server tick.
     */
    void gameStateTick()
    {
        PROFILED_FUNCTION();

        // Offline play always updates once. Its speed is selected by the outer scheduler cadence.
        uint32_t numUpdates = 1;

        // 0x006E3AEC // screen_game_process_mouse_input();
        ScreenshotCheck();
        GameHandleKeyboardInput();

        if (GameIsNotPaused() && gPreviewingTitleSequenceInGame)
        {
            auto player = GetContext()->GetUiContext().GetTitleSequencePlayer();
            if (player != nullptr)
            {
                player->Update();
            }
        }

        auto networkMode = Network::GetMode();
        if (networkMode != Network::Mode::none)
        {
            Network::Update();
            networkMode = Network::GetMode();
        }
        const bool isNetworked = networkMode != Network::Mode::none;

        if (networkMode == Network::Mode::client && Network::GetStatus() == Network::Status::connected
            && Network::GetAuthstatus() == Network::Auth::ok)
        {
            numUpdates = std::clamp<uint32_t>(Network::GetServerTick() - getGameState().currentTicks, 0, 10);
        }
        else
        {
            // Network sessions retain the legacy per-network-tick speed multiplier.
            if (isNetworked && gGameSpeed > 1)
            {
                numUpdates = GetGameSpeedMultiplier(gGameSpeed);
            }
        }

        bool isPaused = GameIsPaused();
        if (networkMode == Network::Mode::server && Config::Get().network.pauseServerIfNoClients)
        {
            // If we are headless we always have 1 player (host), pause if no one else is around.
            if (gOpenRCT2Headless && Network::GetNumPlayers() == 1)
            {
                isPaused = true;
            }
        }

        bool didRunSingleFrame = false;
        if (isPaused)
        {
            if (gDoSingleUpdate && networkMode == Network::Mode::none)
            {
                didRunSingleFrame = true;
                PauseToggle();
                numUpdates = 1;
            }
            else
            {
                // NOTE: Here are a few special cases that would be normally handled in UpdateLogic.
                // If the game is paused it will not call UpdateLogic at all.
                numUpdates = 0;

                if (networkMode == Network::Mode::server)
                {
                    // Make sure the client always knows about what tick the host is on.
                    Network::SendTick();
                }

                // Keep updating the money effect even when paused.
                auto& gameState = getGameState();
                gameState.entities.UpdateMoneyEffect();

                // Post-tick network update
                if (isNetworked)
                {
                    Network::PostTick();
                }

                // Post-tick game actions.
                GameActions::ProcessQueue(gameState);
                gameState.entities.UpdateEntitiesSpatialIndex();
            }
        }

        // A connected network has to tick even when simulation updates are paused.
        if (isNetworked && numUpdates == 0)
        {
            Network::Tick();
        }

        // Update the game one or more times. Only network catch-up can contain multiple logical updates; audio samples its
        // final state instead of rescanning vehicles and visible guests after every intermediate network state.
        bool didUpdatePresentationAudio = false;
        for (uint32_t i = 0; i < numUpdates; i++)
        {
            const bool updatePresentationAudio = i + 1 == numUpdates;
            gameStateUpdateLogic(updatePresentationAudio);
            didUpdatePresentationAudio |= updatePresentationAudio;

            if (gGameSpeed == 1)
            {
                if (InputGetState() == InputState::reset || InputGetState() == InputState::normal)
                {
                    if (gInputFlags.has(InputFlag::viewportScrolling))
                    {
                        gInputFlags.unset(InputFlag::viewportScrolling);
                        break;
                    }
                }
                else
                {
                    break;
                }
            }
            // Don't call UpdateLogic again if the game was just paused.
            if (GameIsPaused())
                break;
        }

        // Input or a pause action can end a network catch-up batch before its planned final update.
        if (numUpdates != 0 && !didUpdatePresentationAudio)
        {
            _presentationAudioPending = true;
        }

        if (isNetworked)
        {
            Network::Flush();
        }

        if (!gOpenRCT2Headless)
        {
            gInputFlags.unset(InputFlag::viewportScrolling);
        }

        // Always perform autosave check, even when paused
        if (gLegacyScene != LegacyScene::titleSequence && gLegacyScene != LegacyScene::trackDesigner
            && gLegacyScene != LegacyScene::trackDesignsManager)
        {
            ScenarioAutosaveCheck();
        }

        if (didRunSingleFrame && GameIsNotPaused() && gLegacyScene != LegacyScene::titleSequence)
        {
            PauseToggle();
        }

        gDoSingleUpdate = false;
    }

    static void gameStateCreateStateSnapshot()
    {
        PROFILED_FUNCTION();

        IGameStateSnapshots* snapshots = GetContext()->GetGameStateSnapshots();

        auto& snapshot = snapshots->CreateSnapshot();
        snapshots->Capture(snapshot);
        snapshots->LinkSnapshot(snapshot, getGameState().currentTicks, ScenarioRandState().s0);
    }

    void gameStateUpdateLogic(bool updatePresentationAudio)
    {
        PROFILED_FUNCTION();

        gInUpdateCode = true;

        gScreenAge++;
        if (gScreenAge == 0)
            gScreenAge--;

        GetContext()->GetReplayManager()->Update();

        const bool isNetworked = Network::GetMode() != Network::Mode::none;
        if (isNetworked)
        {
            Network::Tick();
        }

        auto& gameState = getGameState();

        const auto networkMode = isNetworked ? Network::GetMode() : Network::Mode::none;
        if (networkMode == Network::Mode::server)
        {
            if (Network::GamestateSnapshotsEnabled())
            {
                gameStateCreateStateSnapshot();
            }

            // Send current tick out.
            Network::SendTick();
        }
        else if (networkMode == Network::Mode::client)
        {
            // Don't run past the server, this condition can happen during map changes.
            if (Network::GetServerTick() == gameState.currentTicks)
            {
                gInUpdateCode = false;
                return;
            }

            // Check desync.
            bool desynced = Network::CheckDesynchronisation();
            if (desynced)
            {
                // If desync debugging is enabled and we are still connected request the specific game state from server.
                if (Network::GamestateSnapshotsEnabled() && Network::GetStatus() == Network::Status::connected)
                {
                    // Create snapshot from this tick so we can compare it later
                    // as we won't pause the game on this event.
                    gameStateCreateStateSnapshot();

                    Network::RequestGamestateSnapshot();
                }
            }
        }

#ifdef ENABLE_SCRIPTING
        // Stash the current day number before updating the date so that we
        // know if the day number changes on this tick.
        auto day = gameState.date.GetDay();
#endif

        DateUpdate(gameState);

        ScenarioUpdate(gameState);
        Weather::update();
        MapUpdateTiles();

        // Temporarily remove provisional paths to prevent peep from interacting with them
        auto removeProvisionalIntent = Intent(INTENT_ACTION_REMOVE_PROVISIONAL_ELEMENTS);
        ContextBroadcastIntent(&removeProvisionalIntent);

        MapUpdatePathWideFlags();
        PathFinding::PrepareSharedRouteFields();
        PeepUpdateAll();
        auto restoreProvisionalIntent = Intent(INTENT_ACTION_RESTORE_PROVISIONAL_ELEMENTS);
        ContextBroadcastIntent(&restoreProvisionalIntent);
        VehicleUpdateAll();
        gameState.entities.UpdateAllMiscEntities();
        Ride::updateAll();

        if (!isInEditorMode())
        {
            auto& park = gameState.park;
            Park::Update(park, gameState);
        }

        ResearchUpdate();
        RideRating::UpdateAll();
        RideMeasurementsUpdate();
        News::UpdateCurrentItem();

        MapAnimations::InvalidateAndUpdateAll();
        if (updatePresentationAudio)
        {
            // These scans only select wall-clock audio emitters. Coalesce them at the ordinary UI/frame boundary when
            // several authoritative logical ticks complete before one frame can be presented.
            _presentationAudioPending = true;
        }
        EditorScene::OpenWindowsForCurrentStep();

        // Update windows
        // WindowDispatchUpdateAll();

        gameState.entities.UpdateEntitiesSpatialIndex();

        // Start autosave timer after update
        if (gLastAutoSaveUpdate == kAutosavePause)
        {
            gLastAutoSaveUpdate = Platform::GetTicks();
        }

        GameActions::ProcessQueue(gameState);

        if (isNetworked)
        {
            Network::PostTick();
            Network::Flush();
        }

        gameState.currentTicks++;
        gTotalSimulationTicks++;

#ifdef ENABLE_SCRIPTING
        auto& hookEngine = GetContext()->GetScriptEngine().GetHookEngine();
        hookEngine.Call(HookType::intervalTick, true);

        if (day != gameState.date.GetDay())
        {
            hookEngine.Call(HookType::intervalDay, true);
        }
#endif

        gInUpdateCode = false;
    }

    void gameStateUpdatePresentationAudio()
    {
        PROFILED_FUNCTION();

        if (!_presentationAudioPending)
            return;

        _presentationAudioPending = false;
        Ride::updatePresentationAudio();
        VehicleSoundsUpdate();
        PeepUpdateCrowdNoise();
        Weather::updateSound();
    }
} // namespace OpenRCT2
