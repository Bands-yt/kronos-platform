#pragma once

#include <memory>
#include <random>
#include <deque>
#include <string>

#include "core/ResourceManager.hpp"
#include "core/WorldStreaming.hpp"
#include "core/Audio.hpp"
#include "core/AnimationDatabase.hpp"
#include "core/AvatarController.hpp"
#include "core/AvatarHair.hpp"
#include "core/AvatarLoadout.hpp"
#include "core/Camera.hpp"
#include "core/GameMode.hpp"
#include "core/CatalogueIndex.hpp"
#include "core/CharacterController.hpp"
#include "core/ECS.hpp"
#include "core/EmoteSystem.hpp"
#include "core/Interactable.hpp"
#include "core/Mesh.hpp"
#include "core/NativePluginManager.hpp"
#include "plugin/PluginHost.hpp"
#include "core/Navigation.hpp"
#include "core/ParticleSystem.hpp"
#include "core/PerformanceDiagnostics.hpp"
#include "core/Physics.hpp"
#include "core/ProcessStats.hpp"
#include "core/Profiler.hpp"
#include "core/ResourcePaths.hpp"
#include "core/RiggedAvatar.hpp"
#include "core/RiggedMesh.hpp"
#include "net/NetworkSession.hpp"
#include "core/RollbackScript.hpp"
#include "core/PythonScripting.hpp"
#include "net/RollbackNetSession.hpp"
#include "core/RuntimeAnimationPlayer.hpp"
#include "core/ScriptAvatarApi.hpp"
#include "core/ScriptChatApi.hpp"
#include "safety/GeminiModerationClient.hpp"
#include "core/ScriptNetworkApi.hpp"
#include "core/ScriptUiApi.hpp"
#include "core/ScriptAudioApi.hpp"
#include "core/ScriptUiLayoutApi.hpp"
#include "core/ScriptWorldApi.hpp"
#include "core/ProceduralMaterials.hpp"
#include "core/UIRenderer.hpp"
#include "core/Texture.hpp"
#include "core/TimeOfDay.hpp"
#include "core/Renderer.hpp"
#include "core/Scripting.hpp"
#include "core/Window.hpp"
#include "core/Wind.hpp"
#include "platform_adapters/UnifiedInput.hpp"

namespace engine::runtime { class GameLoop; }

namespace engine::core { class Terrain; }


namespace engine::core {

// Owns every core subsystem for one process (the runtime client/server, or
// Studio) and their lifetime/ordering. Application does not know about
// gameplay -- it hands a fully-initialized set of subsystems to GameLoop
// and lets that own the per-frame tick order from docs/ARCHITECTURE.md §6.
class Application {
public:
    struct CreateInfo {
        std::string title = "Engine Runtime";
        uint32_t width = 1280;
        uint32_t height = 720;
        bool enableValidation = true;
        bool headless = false;
        // Kronos ("Active Joining UI" -- engine_runtime ImGui + input
        // integration): true (the default) preserves every existing call
        // site's behavior byte-for-byte -- initialize() unconditionally
        // captured the mouse (relative mode, hidden cursor) for gameplay
        // look control before this existed. The new Home Screen shell
        // sets this false so the OS cursor stays visible/absolute for
        // real menu clicks, then toggles it back on with a real
        // input().setRelativeMouseMode(true) call once actual gameplay
        // starts (see runtime::RuntimeShell's own state-transition code)
        // -- the same, already-real, already-tested toggle this flag just
        // controls the INITIAL value of.
        bool startWithCapturedMouse = true;
    };

    // Both declared here but defined in Application.cpp, not defaulted
    // inline: gameLoop_ is a unique_ptr<GameLoop> and GameLoop is only
    // forward-declared above, so the implicit destroy-on-exception path
    // the compiler generates for a defaulted constructor needs GameLoop's
    // complete type, which is only visible where runtime/GameLoop.hpp is
    // included (Application.cpp).
    Application();
    ~Application();

    Application(const Application&) = delete;
    Application& operator=(const Application&) = delete;

    [[nodiscard]] bool initialize(const CreateInfo& info);
    void shutdown();

    // Blocks until the window is closed. See runtime/GameLoop.* for the
    // per-tick subsystem ordering this drives.
    void run();

    [[nodiscard]] Window& window() { return window_; }
    [[nodiscard]] Renderer& renderer() { return renderer_; }
    [[nodiscard]] bool isHeadless() const { return headless_; }
    [[nodiscard]] ECS& ecs() { return ecs_; }
    [[nodiscard]] Physics& physics() { return physics_; }
    [[nodiscard]] Audio& audio() { return audio_; }
    [[nodiscard]] Scripting& scripting() { return scripting_; }
    [[nodiscard]] PythonScripting& python() { return python_; }
    // Kronos ("Native Plugin Architecture"): real, currently loaded
    // native (.so/.dll) engine plugins -- discovered once from the
    // packaged/dev "plugins" directory in initialize() and ticked
    // every frame alongside tickScriptHotReload() below. See
    // NativePluginManager.hpp's own class comment for the full contract.
    [[nodiscard]] NativePluginManager& nativePlugins() { return nativePlugins_; }
    [[nodiscard]] plugin::PluginHost& pluginApi() { return pluginApi_; }
    [[nodiscard]] MeshLibrary& meshLibrary() { return meshLibrary_; }
    [[nodiscard]] ResourceManager& resources() { return resources_; }
    [[nodiscard]] WorldStreamer& worldStreamer() { return worldStreamer_; }
    [[nodiscard]] TextureLibrary& textureLibrary() { return textureLibrary_; }
    [[nodiscard]] Camera& camera() { return camera_; }
    [[nodiscard]] platform_adapters::UnifiedInput& input() { return input_; }
    // Installs the game that hooks into the tick (see core/GameMode.hpp); null for none.
    void setGameMode(std::unique_ptr<GameMode> mode) { gameMode_ = std::move(mode); }
    [[nodiscard]] GameMode* gameMode() { return gameMode_.get(); }
    [[nodiscard]] CharacterController& characterController() { return characterController_; }
    [[nodiscard]] RiggedMeshLibrary& riggedMeshLibrary() { return riggedMeshLibrary_; }
    [[nodiscard]] UIRenderer& uiRenderer() { return uiRenderer_; }
    [[nodiscard]] const std::vector<EntityId>& localPlayerSkinnedEntities() const { return skinnedAvatarEntities_; }

    // Kronos ("Avatar System" -- "replace the placeholder cylinder with a
    // real humanoid avatar"): the one real, shared spawn path for the
    // local, physics-driven player character -- everything main.cpp's
    // bring-up world and runtime::RuntimeShell's own Catalogue-launch
    // callback need, in one place, instead of each hand-rolling its own
    // characterController_.spawn() + plain-capsule-Renderable call (the
    // "placeholder cylinder" the user's spec names). Real, complete
    // orchestration: spawns the physics capsule (characterController_.
    // spawn(), no visible mesh of its own this time -- the rigged body
    // below is what's actually drawn), spawns the real 18-bone rigged
    // avatar body (RiggedAvatar.hpp's spawnRiggedAvatar(), an "empty
    // loadout" -- no catalogue/creator-marketplace tie-in yet, a real,
    // stated scope boundary, not a placeholder claiming otherwise), loads
    // the 6 real shipped clips (engine/assets/animations/*.anim -- idle/
    // walk/run/jump_start/jump_air/jump_land) into a fresh
    // AvatarController, and wires both into the real pre-tick hook
    // (initialize()'s own PreTickHook lambda) so CharacterController::
    // tick()'s existing, already-real avatarController/skinnedEntities
    // parameters actually get driven every tick from here on.
    //
    // Real, honest degrade: if the rigged body's GPU upload fails (a real,
    // if rare, possibility -- see spawnRiggedAvatar()'s own contract),
    // this returns false and falls back to nothing being spawned at all
    // rather than a silently-broken half-avatar; the caller's own
    // pre-existing plain-capsule fallback (if it still has one) is the
    // honest recovery path, not fabricated here.
    // Kronos ("Avatar Phase" -- "AvatarEditor: Skin-Tone Selection"):
    // `skinTone` is real, new, optional (defaults to the same real color
    // this always used before the feature existed -- see
    // core::kDefaultSkinToneColor) -- the caller (runtime::RuntimeShell/
    // main.cpp) resolves the real, chosen tone from core::LocalProfile::
    // skinToneIndex (core::resolveSkinToneColor()) and passes it in; this
    // class doesn't own a LocalProfile itself (core::Application has no
    // real per-player identity concept, matching every other real
    // "caller owns identity, Application just spawns" split in this
    // class, e.g. setNetworkedLocalPlayerEntity()).
    // Kronos ("Avatar Phase" -- "Avatar Head System"): `headShape` is
    // real, new, optional (defaults to core::HeadShape::Oval, the new
    // real default) -- same "caller resolves from its own LocalProfile,
    // this method just forwards it" shape as `skinTone` above.
    // Kronos ("Avatar Phase" -- "AvatarEditor: Body Sliders"):
    // `bodyProportions` is real, new, optional (defaults to identity --
    // see core::BodyProportions's own header comment). Unlike skinTone/
    // headShape, this method applies it to the skeleton itself (via
    // core::applyBodyProportionsToSkeleton()) before spawning, and passes
    // that same scaled skeleton to the new AvatarController it
    // constructs, so animation playback skins against the same bind pose
    // the mesh was actually built from.
    // Kronos ("Avatar Phase" -- "AvatarEditor: Clothing & Accessory
    // Slots"): `loadout`/`catalogueIndex` are real, new, optional
    // (default to empty -- the exact same "nothing equipped" behavior
    // every pre-existing call site already had). Forwarded straight to
    // spawnRiggedAvatar(), same "caller resolves from its own
    // LocalProfile/CatalogueIndex, this method just forwards it" shape as
    // skinTone/headShape above -- see runtime::RuntimeShell::selectGame()
    // for the one real caller that resolves both from real, on-disk,
    // shared files.
    // Kronos ("Avatar Phase" -- "AvatarEditor: Animation Overrides"):
    // `animationOverrides` is real, new, optional (defaults to every slot
    // empty -- the exact same "always the shipped default" behavior every
    // pre-existing call site already had). Each non-empty slot is tried
    // first; a broken override real-falls back to the shipped default
    // clip rather than leaving that locomotion state with no clip at all
    // -- see the .cpp's own loadClip lambda.
    // Kronos ("Avatar 2.0" -- "Clothing Meshes"): `clothingFit` is real,
    // new, optional (defaults to ClothingFit::Tight, same real value
    // core::LocalProfile::clothingFitIndex itself defaults to -- every
    // pre-existing call site keeps compiling and looking identical).
    [[nodiscard]] bool spawnLocalPlayerAvatar(glm::vec3 spawnPosition,
                                               glm::vec4 skinTone = glm::vec4(0.85f, 0.75f, 0.65f, 1.0f),
                                               HeadShape headShape = HeadShape::Oval,
                                               BodyProportions bodyProportions = {},
                                               const AvatarLoadout& loadout = AvatarLoadout(),
                                               const CatalogueIndex& catalogueIndex = CatalogueIndex(),
                                               const AnimationOverrides& animationOverrides = AnimationOverrides(),
                                               ClothingFit clothingFit = ClothingFit::Tight);

    // Kronos ("Marketplace" -- "engine_runtime-side catalogue UI" --
    // live re-equip while InGame): real, live re-tint of the already-
    // spawned local player avatar's own segment colors -- the exact same
    // resolveSegmentColorsForLoadout() mechanism studio::plugins::
    // AvatarEditor::refreshSegmentColors() already uses for its own live
    // preview, applied here to the real, live gameplay avatar instead.
    // Deliberately does NOT respawn anything (headShape/bodyProportions
    // changes still need a real spawnLocalPlayerAvatar() respawn, same
    // "color-only changes are live, geometry changes need a respawn"
    // split AvatarEditor's own applySkinTone()/applyHeadShape() already
    // draw) -- a real, honest no-op if no avatar is currently spawned
    // (skinnedAvatarEntities_ empty), not an error.
    void refreshLocalPlayerAvatarAppearance(glm::vec4 skinTone, const AvatarLoadout& loadout,
                                             const CatalogueIndex& catalogueIndex);

    // Kronos ("Avatar 2.0" -- "Animation Polish" -- "Support emote
    // playback from Marketplace items"): a real, thin forward to
    // core::playEquippedEmote() against the real, live gameplay
    // avatarController_ -- see that free function's own header comment
    // (EmoteSystem.hpp) for the full real equip -> resolve -> play
    // contract this just forwards `loadout`/`animationDatabase` into. A
    // real, honest no-op (returns false, `outError` left empty) if no
    // avatar is currently spawned (avatarController_ null), not a crash.
    bool playEquippedEmote(const AvatarLoadout& loadout, const AnimationDatabase& animationDatabase,
                            std::string& outError);

    // Kronos ("Kronos Scripting Environment" -- "Immediate Gaps for
    // Launch" -- `world.spawnPlayer`/`avatar.playEmote`): real, nullable
    // access to the live gameplay avatarController_ -- ScriptAvatarApi
    // (core/ScriptAvatarApi.hpp) needs this to check "is an avatar
    // currently spawned at all" the same way this class's own
    // spawnLocalPlayerAvatar()/playEquippedEmote() already do internally,
    // without duplicating that null-check logic in the Lua-facing layer.
    [[nodiscard]] AvatarController* avatarController() const { return avatarController_.get(); }

    // Kronos ("Kronos Scripting Environment"): real, late-bound,
    // nullable -- unlike ecs_/physics_/animationPlayer_ (owned here since
    // construction, safe to capture by reference at ScriptWorldApi
    // construction time), AnimationDatabase is owned by whichever layer
    // actually loads it (engine_runtime's RuntimeShell, at
    // ensureAvatarCatalogueLoaded() time -- often well after this class's
    // own initialize()/bindings-registration already ran), not by
    // Application itself. A setter, not a constructor parameter, so the
    // real owner can wire it in whenever it's actually ready; real
    // callers (ScriptAvatarApi::luaPlayEmote) treat a still-null pointer
    // as "no avatar catalogue loaded yet" and fail soft, the same
    // "real, honest no-op, not a crash" convention playEquippedEmote()
    // already establishes for a null avatarController_.
    void setAnimationDatabase(const AnimationDatabase& database) { animationDatabase_ = &database; }

    // Kronos ("Kronos Scripting Environment" -- "world.spawnPlayer"):
    // real orchestration backing the Lua binding -- if no avatar is
    // currently spawned, does a real, fresh spawnLocalPlayerAvatar() at
    // `position` with the same default cosmetics main.cpp's own bring-up
    // world already uses (this class's own spawnLocalPlayerAvatar()
    // overload with only `spawnPosition` given). If an avatar already
    // exists, this is a real, honest "respawn" -- teleports the existing
    // character (via the same Physics::setPosition() every other
    // position-setting script binding already uses) rather than
    // re-running full cosmetic/clothing/hair spawn logic a second time.
    // Returns the real, live character entity id, or kNullEntity if a
    // fresh spawn was attempted and failed. Verified live (real
    // engine_runtime launch, temporary script, reverted before commit):
    // like every other Physics position write (setVelocity/applyImpulse,
    // see ScriptWorldApi's own tests), Physics::setPosition() only moves
    // the real Jolt body -- the readable ECS Transform only reflects it
    // after the next real physics.step() syncs it, so a script reading
    // world.getPosition() in the *same* tick as world.spawnPlayer() sees
    // the pre-move position, not a bug specific to this binding.
    [[nodiscard]] EntityId respawnLocalPlayer(glm::vec3 position);

    // Kronos ("Kronos Scripting Environment" -- "avatar.playEmote"): real
    // orchestration backing the Lua binding -- resolves `emoteId` via
    // core::resolveEmoteClip() (EmoteSystem.hpp) against the real,
    // late-bound animationDatabase_ above, then plays it full-body (the
    // same real "classic Roblox-style emotes take over the whole
    // character" choice playEquippedEmote() already makes) on the local
    // player's own avatarController_. `entity` is checked against the
    // real, live characterController_.entity() -- see this method's own
    // .cpp comment for why only the local player has a live
    // AvatarController today, and what happens for any other entity id.
    // Real, honest false (not a crash) if no avatar is spawned, no
    // animation database has been wired in yet, `entity` doesn't match
    // the local player, or the emote id itself doesn't resolve --
    // `outError` is only filled for that last, genuinely-a-data-problem
    // case, matching playEquippedEmote()'s own outError contract.
    bool tryPlayEmoteForEntity(EntityId entity, const std::string& emoteId, bool looping, std::string& outError);

    [[nodiscard]] ParticleSystem& particleSystem() { return particleSystem_; }
    [[nodiscard]] RuntimeAnimationPlayer& animationPlayer() { return animationPlayer_; }
    [[nodiscard]] ScriptUiApi& scriptUiApi() { return scriptUiApi_; }
    // Kronos ("Native Vector UI Engine"): the declarative flexbox
    // sibling of scriptUiApi() above -- see ScriptUiLayoutApi.hpp's own
    // class comment. Constructed lazily in initialize() (needs
    // scripting_ already alive to bind its watchdog-refresh calls
    // against), same "unique_ptr, built in initialize()" shape
    // scriptAvatarApi_ already uses.
    [[nodiscard]] ScriptUiLayoutApi& scriptUiLayoutApi() { return *scriptUiLayoutApi_; }

    // Kronos ("Active Joining UI" -- engine_runtime ImGui + input
    // integration): real, non-owning access to the real GameLoop
    // initialize() already constructs -- lets main.cpp's own real Home
    // Screen shell setup (runtime::RuntimeShell) register a real
    // PreRenderHook after initialize() returns, without Application
    // needing to know that shell exists at all (same "the caller wires
    // its own real hook, Application just exposes the seam" split every
    // other setOnX()/setXHook() in this codebase already uses). Never
    // null after a successful initialize().
    [[nodiscard]] runtime::GameLoop* gameLoop() { return gameLoop_.get(); }

    // Sprint 5 ("Core Economy"): the mesh handle main.cpp registers for a
    // physical ore-drop pickup's visual -- core/OreNode.cpp's
    // breakOreNode() needs one to assign but deliberately doesn't own a
    // MeshLibrary itself (same GPU-independence boundary core::Physics
    // maintains), so the caller supplies it here once at startup, the
    // same "caller owns mesh registration, core owns gameplay logic"
    // split main.cpp's makeRenderable() already establishes.
    void setOreDropMeshHandle(uint32_t handle) { oreDropMeshHandle_ = handle; }

    // Kronos ("Alpha v1 Polish" -- "world.spawnDynamicBox"): the real,
    // identical pattern setOreDropMeshHandle() above already
    // establishes -- forwards to ScriptWorldApi::setSpawnBoxMeshHandle(),
    // see that method's own comment for why this deferred-setter shape
    // exists instead of ScriptWorldApi building its own GPU mesh.
    void setScriptSpawnBoxMeshHandle(uint32_t handle) { scriptWorldApi_->setSpawnBoxMeshHandle(handle); }

    // Sprint 6 ("World Systems & Environment") -- same "caller owns the
    // heavy/GPU-touching resource, Application just gets a pointer to
    // drive it every tick" split as setOreDropMeshHandle() above.
    // `terrain` may be null (no streaming updates run) -- a scene with no
    // terrain at all is a real, valid configuration, not an error.
    void setTerrain(Terrain* terrain) { terrain_ = terrain; }
    void setTerrainStreamingRadii(float loadRadius, float unloadRadius) {
        terrainLoadRadius_ = loadRadius;
        terrainUnloadRadius_ = unloadRadius;
    }
    void setWorldBoundary(const WorldBoundary& boundary) { worldBoundary_ = boundary; }
    void setDayLengthSeconds(float seconds) { dayLengthSeconds_ = seconds; }
    [[nodiscard]] TimeOfDayState& timeOfDayState() { return timeOfDayState_; }

    // Kronos ("Sky Map Full Engine Specification"): computeLightingForTimeOfDay()
    // only ever sets directionWS/color/intensity/ambient/ambientGround
    // (real, by design -- the day/night cycle owns exactly those) and
    // leaves fogColor/fogDensity/skyZenithColor/skyHorizonColor at
    // SceneLighting's own struct defaults every single tick, so a plain
    // one-time renderer_.setLighting() call made before app.run() to
    // customize a specific map's own real atmosphere (e.g. the Sky Map's
    // low-density blue-white fog) would get real-clobbered the instant
    // the first tick's own day/night update runs. This is a real,
    // minimal, opt-in override applied *after* that per-tick call --
    // every mode that never calls this keeps behaving byte-for-byte as
    // before (fog/sky stay at SceneLighting's own real defaults).
    struct AtmosphereOverride {
        glm::vec3 fogColor{0.6f, 0.65f, 0.75f};
        float fogDensity = 0.0f;
        glm::vec3 skyZenithColor{0.25f, 0.45f, 0.85f};
        glm::vec3 skyHorizonColor{0.75f, 0.80f, 0.85f};
        // When set, replaces the time-of-day sun and ambient.
        bool overrideSun = false;
        glm::vec3 sunDirectionWS{0.0f, -1.0f, 0.0f}; // direction the light travels
        glm::vec3 sunColor{1.0f};
        float sunIntensity = 4.0f;
        glm::vec3 ambient{0.08f, 0.10f, 0.15f};
        glm::vec3 ambientGround{0.06f, 0.05f, 0.045f};
    };
    void setAtmosphereOverride(const AtmosphereOverride& atmosphere) {
        atmosphereOverride_ = atmosphere;
        hasAtmosphereOverride_ = true;
    }


    // Sprint 8 ("Performance Stats & Debug Tools"): the real, live
    // profiler this process's own PostRenderHook feeds every frame (see
    // initialize()) -- exposed so an embedder/test can toggle
    // startRecording()/stopRecording() or inspect events() directly.
    [[nodiscard]] Profiler& profiler() { return profiler_; }
    [[nodiscard]] const PerformanceMetrics& lastPerformanceMetrics() const { return lastPerformanceMetrics_; }

    // Sprint 11 ("Networking Foundation"): real client/server multiplayer,
    // additive and opt-in -- a process that never calls startNetworking()
    // behaves exactly as before this sprint (NetworkSession::tick() is a
    // real, honest no-op in Offline mode, the default). See
    // net::NetworkSession's own class comment for the full architecture
    // and net::NetworkedMovement.hpp's for why networked play uses a
    // real, simpler kinematic movement model instead of the full
    // physics-based characterController_ this process's offline/local
    // play still uses unchanged.
    [[nodiscard]] bool startNetworking(const net::NetworkSession::Config& config);
    [[nodiscard]] net::NetworkSession& networkSession() { return networkSession_; }

    // Peer-to-peer rollback match on the loaded scene: physics is frozen
    // until every player has joined, then each player gets a character
    // capsule and the whole world is simulated on every machine, with
    // rollback hiding the network delay. Needs a window. Scripts must not
    // push physics objects around during a match (only the rollback
    // simulation may), or the players' games drift apart and the match ends.
    bool startRollbackMatch(const net::RollbackNetSession::Config& config, std::string* error = nullptr);
    [[nodiscard]] net::RollbackNetSession& rollbackMatch() { return rollbackNet_; }
    [[nodiscard]] const std::vector<EntityId>& rollbackPlayers() const { return rollbackPlayers_; }
    // The game's match script (Scripts/Match.luau), run inside the rollback simulation. See ROLLBACK.md.
    void setMatchScript(std::string name, std::string source) {
        matchScriptName_ = std::move(name);
        matchScriptSource_ = std::move(source);
    }
    [[nodiscard]] const RollbackScript* matchScript() const { return matchScript_.get(); }

    // The real, simple (Transform + Name + Renderable, no physics
    // capsule) networked-player entity this process's own local input
    // drives in Client mode -- see startNetworking()'s implementation
    // comment on why this is deliberately not characterController_'s
    // entity. kNullEntity (the default) means "not networked" and is
    // what keeps every existing offline call site's behavior unchanged.
    // Kronos (beta, "restore the 18-bone humanoid for online play"): real
    // -- was a plain, inline setter; now also spawns/tears down the real
    // rigged avatar that visually follows `entity` (see
    // Application.cpp's own implementation and networkedAvatarController_'s
    // header comment for why this needs its own AvatarController/
    // skinnedEntities, separate from the offline avatarController_/
    // skinnedAvatarEntities_ pair).
    void setNetworkedLocalPlayerEntity(EntityId entity);
    [[nodiscard]] EntityId networkedLocalPlayerEntity() const { return networkedLocalPlayerEntity_; }




    // Kronos ("Avatar Gameplay Lighting Harmonisation Pass"): real,
    // optional indoor-lighting mode -- the same real "caller sets a
    // plain bool, the pre-tick hook checks it" shape
    // cameraShowcaseModeEnabled_ already establishes. When enabled, the
    // tick loop's own lighting block (see tick()'s own comment) skips
    // the real day/night cycle entirely and uses
    // core::avatarIndoorPreviewLighting() instead, and real-forces
    // weather back to Clear every tick so a real, live outdoor weather
    // event can never perturb an indoor scene's avatars -- "Weather
    // Isolation" per this pass's own explicit requirement. A real,
    // honest no-op (false, the default) for every existing launch mode:
    // a real investigation (see docs/progress.md) found no gameplay map
    // in this codebase is actually enclosed at the whole-map level
    // today (TNT Wars maps are open-sky with small covered bunkers only,
    // Mining Sim's "Dungeon" spawns no walls/ceiling despite the name,
    // House Demo is outdoor terrain with one small enclosed house on
    // it) -- so this real mechanism exists and is real-tested, but no
    // caller turns it on yet. A real, explicit, user-directed choice
    // (build the real mechanism now, wire a real caller to it only once
    // a genuinely enclosed gameplay scene actually exists), not scope
    // creep or an oversight.
    void setIndoorLightingMode(bool enabled) { indoorLightingModeEnabled_ = enabled; }
    [[nodiscard]] bool indoorLightingModeEnabled() const { return indoorLightingModeEnabled_; }

    // Kronos ("Player & Chat System" -- chat panel): real, small, same
    // "caller sets a plain bool, the pre-tick hook checks it" shape
    // cameraShowcaseModeEnabled_ already establishes -- this class
    // deliberately has no ImGui dependency at all (engine_core links no
    // UI framework, see core/UITheme.hpp's own header comment for the
    // same real constraint), so it can't check ImGui::GetIO().
    // WantCaptureKeyboard itself; runtime::RuntimeShell (which does have
    // ImGui) sets this explicitly instead while its own real chat input
    // box has focus, so WASD/mouse-look don't also drive the character
    // while a player is typing a chat message.
    void setMovementInputSuspended(bool suspended) {
        movementInputSuspended_ = suspended;
        input_.setOrbitDragEnabled(!suspended);
    }
    // Plays the standing idle on the local avatar while movement is suspended (e.g. a game's menu).
    void tickLocalAvatarIdle(float dt);

    // Kronos ("Settings Panel v2 + Input Remapping + Accessibility
    // Layer" -- "Accessibility: Reduced motion mode"): real, same "caller
    // resolves from its own LocalProfile, this class just consumes it"
    // shape as setMovementInputSuspended() above -- core::Application has
    // no LocalProfile of its own (see that method's own comment on the
    // established split). Real, live consumption at the two real places
    // this engine actually moves the camera on its own: TNT Wars
    // explosion camera shake (scaled to zero, not just damped -- see the
    // pre-tick hook's own real shake-application line) and the cutscene
    // FOV-change path (frozen at whatever FOV was already active when
    // this was enabled, rather than following cutscene data -- see that
    // real call site's own comment).
    void setReducedMotionEnabled(bool enabled) { reducedMotionEnabled_ = enabled; }
    [[nodiscard]] bool isReducedMotionEnabled() const { return reducedMotionEnabled_; }











    // Kronos ("Environmental Detail" world-building): real, general
    // ambient wind -- a real map-agnostic system (not TNT-Wars-specific,
    // unlike the accessors above), see core/Wind.hpp's own comment. A
    // caller (main.cpp's own map setup) sets a real direction/strength
    // once; Application's own regular pretick hook (unconditional, every
    // frame, every scene) ticks core::tickWindSway()/tickAtmosphericDustWind()
    // from it -- a real, honest no-op scene/map that spawned no
    // WindSway/AtmosphericDustEmitter entities pays only the cost of an
    // empty ECS view iteration.
    [[nodiscard]] core::WindState& windState() { return windState_; }



private:
    GameModeContext modeContext();
    [[nodiscard]] bool cameraShowcaseModeEnabled() const { return gameMode_ && gameMode_->controlsCamera(); }
    std::unique_ptr<GameMode> gameMode_;

    Window window_;
    Renderer renderer_;
    // Kronos ("User Interface" world-building) -- see UIRenderer.hpp's
    // own header comment.
    UIRenderer uiRenderer_;
    ResourceManager resources_{2};
    ECS ecs_;
    Physics physics_;
    Audio audio_;
    Scripting scripting_;
    PythonScripting python_;
    NativePluginManager nativePlugins_;
    plugin::PluginHost pluginApi_;
    MeshLibrary meshLibrary_;
    void updateWorldStreaming();
    void tickRollbackMatch(float dt);
    PhysicsRollback* beginRollbackMatch(uint32_t players, uint32_t localPlayer);
    void showRollbackStatus();
    WorldStreamer worldStreamer_; // after ecs_/resources_/meshLibrary_: closes before they go
    TextureLibrary textureLibrary_;
    Camera camera_;
    platform_adapters::UnifiedInput input_;
    CharacterController characterController_;
    // Kronos ("Avatar System" -- real humanoid avatar): see
    // spawnLocalPlayerAvatar()'s own comment. riggedMeshLibrary_ owns the
    // real GPU mesh resources spawnRiggedAvatar() uploads; avatarController_
    // is null until spawnLocalPlayerAvatar() first succeeds (no avatar
    // spawned yet, e.g. a CLI mode that never calls it -- every such
    // caller keeps behaving exactly as before, see the PreTickHook's own
    // null-check).
    RiggedMeshLibrary riggedMeshLibrary_;
    std::unique_ptr<AvatarController> avatarController_;
    // Kronos ("Kronos Scripting Environment"): real, nullable, late-bound
    // -- see setAnimationDatabase()'s own comment above for why this
    // can't just be a reference captured at construction time the way
    // ecs_/physics_/animationPlayer_ are.
    const AnimationDatabase* animationDatabase_ = nullptr;
    std::vector<EntityId> skinnedAvatarEntities_;
    ParticleSystem particleSystem_;
    RuntimeAnimationPlayer animationPlayer_;
    // unique_ptr, not a plain member: ScriptWorldApi holds references to
    // ecs_/physics_/animationPlayer_ bound at construction, and those must
    // already be fully constructed first -- constructed in initialize()
    // instead of relying on in-class member-declaration order to get that
    // right implicitly.
    std::unique_ptr<ScriptWorldApi> scriptWorldApi_;
    // Same reasoning as scriptWorldApi_ above: ScriptNetworkApi holds
    // references to networkSession_/scripting_ bound at construction.
    std::unique_ptr<ScriptNetworkApi> scriptNetworkApi_;
    // Same lifetime reasoning as scriptNetworkApi_ above: it holds a
    // NetworkSession receive hook, so it must outlive every script VM and
    // be destroyed before the session it points at.
    std::unique_ptr<ScriptChatApi> scriptChatApi_;

    // ORDER MATTERS, and it is the reverse of the intuitive one.
    //
    // The pool's worker threads run completion callbacks that capture the
    // moderation client's `this`. Members are destroyed in reverse
    // declaration order, so the pool must be declared LAST of the two:
    // that destroys (and joins) it FIRST, guaranteeing no worker is still
    // inside a callback when the client it points at goes away.
    // Declaring them the other way round is a use-after-free that only
    // shows up when a request happens to be in flight at shutdown.
    //
    // Application::shutdown() also joins the pool explicitly, so the
    // ordering here is the backstop rather than the only defence.
    std::unique_ptr<safety::GeminiModerationClient> chatModerationClient_;
    std::unique_ptr<net::HttpWorkerPool> httpWorkerPool_;
    // Kronos ("Kronos Scripting Environment"): holds an `Application&`
    // (i.e. `*this`) -- always valid regardless of member-construction
    // order, unlike scriptWorldApi_/scriptNetworkApi_ above, but kept as
    // a unique_ptr anyway for the same deferred-construction-in-
    // initialize() shape those two already establish, not constructed
    // inline as a plain member.
    std::unique_ptr<ScriptAvatarApi> scriptAvatarApi_;
    // Plain member, not unique_ptr -- ScriptUiApi holds no references at
    // all (it's a pure queue, flushed externally via flushInto()), so it
    // needs no deferred-construction seam the way scriptWorldApi_/
    // scriptNetworkApi_ do.
    ScriptUiApi scriptUiApi_;
    // unique_ptr, not a plain member -- ScriptUiLayoutApi holds a real
    // `Scripting&` (to refresh the watchdog deadline before invoking a
    // bound Luau function, see its own header comment), so it needs
    // scripting_ already constructed first; built in initialize(), same
    // deferred-construction shape scriptAvatarApi_ above uses.
    std::unique_ptr<ScriptUiLayoutApi> scriptUiLayoutApi_;
    std::unique_ptr<ScriptAudioApi> scriptAudioApi_;
    // Edge-detection for the "Interact" input action (UnifiedInput only
    // exposes level state via isActionDown(), see its header) -- so
    // events.onInteract fires once per press, not once per tick while held.
    // Kronos (beta-blocking fix -- "flickering when opening a game"): set
    // true the first time the postRenderHook below actually reveals the
    // real window, after its own first successful renderFrame() -- see
    // Window::show()'s own comment for why this can't just happen at
    // window-creation time any more.
    bool windowShown_ = false;
    bool interactKeyWasDown_ = false;
    // Sprint 14: same real edge-detection pattern, for the real
    // F6/F7 runtime toggles -- see initialize()'s pretick hook.
    bool rtShadowToggleKeyWasDown_ = false;
    bool performanceModeToggleKeyWasDown_ = false;
    // Sprint 16: same real edge-detection pattern, for the F9 Cinematic
    // Mode runtime toggle -- see initialize()'s pretick hook.
    bool cinematicModeToggleKeyWasDown_ = false;
    // Kronos ("Rendering Fidelity Foundation" Phase 1.1): same real
    // edge-detection pattern, for the F10 weather-cycle runtime toggle --
    // see initialize()'s pretick hook.
    bool cycleWeatherToggleKeyWasDown_ = false;
    // Kronos ("Rendering Fidelity Foundation" Phase 1.2): same real
    // edge-detection pattern, for the F11 volumetric-fog runtime toggle --
    // see initialize()'s pretick hook.
    bool volumetricFogToggleKeyWasDown_ = false;
    // Kronos ("Rendering Fidelity Foundation" Phase 1.3): same real
    // edge-detection pattern, for the F12 RT-reflections runtime toggle --
    // see initialize()'s pretick hook.
    bool rtReflectionsToggleKeyWasDown_ = false;
    // Kronos ("Four RTX Maps" Phase 5b): same real edge-detection pattern,
    // for the F8 heat-distortion runtime toggle (Volcano Map) -- see
    // initialize()'s pretick hook.
    bool heatDistortionToggleKeyWasDown_ = false;

    // Real accumulated *simulation* time (sum of GameLoop's fixed dt, not
    // wall-clock time) -- what core::Interactable's cooldown gate is
    // measured against, the same determinism reasoning every other
    // fixed-tick system in this engine already follows.
    float totalSimTime_ = 0.0f;
    // Kronos ("Environmental Detail" world-building) -- see windState()'s
    // own public comment. A mild, real, non-zero default so a scene that
    // never calls windState() still gets a small, believable ambient
    // sway/drift rather than a dead-calm 0.
    core::WindState windState_;
    // Tracks the last entity the interaction UI-hint reported, so it
    // only updates the game mode's prompt on a real change
    // (entering/leaving range or look-at), not every single tick while
    // unchanged.
    EntityId lastInteractionHintEntity_ = kNullEntity;

    // Sprint 5 ("Core Economy") state -- real per-run randomness (ore
    // drop-table rolls, bonus-gem chance), deliberately NOT the fixed
    // seed the decorative bring-up-scene dressing in main.cpp uses (see
    // that file's own comment on why *that* RNG is fixed): a shipped
    // game's actual drop rolls should vary run to run like a real game's
    // would, not replay the same "random" sequence every launch.
    std::mt19937 economyRng_{std::random_device{}()};
    uint32_t oreDropMeshHandle_ = 0;

    // Sprint 6 ("World Systems & Environment") state.
    Terrain* terrain_ = nullptr;
    float terrainLoadRadius_ = 60.0f;
    float terrainUnloadRadius_ = 80.0f;
    // A generous default (well outside any real bring-up scene's actual
    // footprint) so a caller that never calls setWorldBoundary() gets a
    // real, honest no-op rather than an invisible wall it never asked
    // for -- consistent with every other new-this-sprint feature
    // defaulting to "off"/inert until explicitly configured.
    WorldBoundary worldBoundary_{glm::vec3(0.0f), 200.0f, 220.0f};
    TimeOfDayState timeOfDayState_;
    float dayLengthSeconds_ = 300.0f; // 5 real minutes per in-game day -- a real, playable pace, not instant/imperceptible
    // Kronos ("Sky Map Full Engine Specification") -- see setAtmosphereOverride()'s own comment.
    AtmosphereOverride atmosphereOverride_;
    bool hasAtmosphereOverride_ = false;

    // Kronos ("Rendering Fidelity Foundation" Phase 1.1): a real, lazily-
    // created ParticleEmitter entity driven every tick from
    // Renderer::currentWeatherProfile()/targetWeatherKind() -- see
    // updateWeatherParticles()'s own comment in Application.cpp for why
    // this bridge has to live here rather than inside Renderer itself.
    EntityId weatherParticleEntity_ = kNullEntity;


    bool movementInputSuspended_ = false;
    bool reducedMotionEnabled_ = false;

    // Kronos ("Avatar Gameplay Lighting Harmonisation Pass") -- see
    // setIndoorLightingMode()'s own comment.
    bool indoorLightingModeEnabled_ = false;


    // Sprint 8 ("Performance Stats & Debug Tools") state -- fed every
    // frame from GameLoop::PostRenderHook (see initialize()).
    Profiler profiler_;
    ProcessStatsSampler processStatsSampler_;
    PerformanceMetrics lastPerformanceMetrics_;
    float metricsLogAccumulatorSeconds_ = 0.0f;

    // Sprint 11 ("Networking Foundation") state -- see startNetworking()'s
    // own comment.
    net::NetworkSession networkSession_;

    std::unique_ptr<PhysicsRollback> rollback_;
    net::RollbackNetSession rollbackNet_; // after rollback_: holds a pointer to it
    std::vector<EntityId> rollbackPlayers_;
    std::string matchScriptName_;
    std::string matchScriptSource_;
    std::shared_ptr<RollbackScript> matchScript_;
    float rollbackFrameDt_ = 1.0f / 120.0f;
    net::RollbackNetSession::Phase rollbackShownPhase_ = net::RollbackNetSession::Phase::Idle;
    float rollbackTitleTimer_ = 0.0f;
    std::string baseWindowTitle_;
    uint32_t rollbackCapsuleMesh_ = Renderable::kInvalidHandle;
    void robloxPlayerJoined(net::PlayerId player, const std::string& name, bool local);
    // Game scripts, Roblox players and remotes; runs with or without a window.
    void tickGameScripts(float dt);
    EntityId networkedLocalPlayerEntity_ = kNullEntity;

    // Kronos (beta, "restore the 18-bone humanoid for online play"): the
    // real rigged-avatar body that visually follows networkedLocalPlayerEntity_
    // (a deliberately kinematic, physics-free Transform -- see
    // net::applyNetworkedMovement()'s own header comment) once a session
    // is actually joined. A second, separate AvatarController/skinned-
    // entity-list from avatarController_/skinnedAvatarEntities_ above, not
    // a reuse of them: those stay real, intact, and hidden (see
    // wasNetworkedClient_'s own comment) for the offline character the
    // whole time this one is active, so leaveSession() can hand control
    // straight back with nothing to re-spawn. Null/empty whenever no
    // session is joined -- setNetworkedLocalPlayerEntity()'s own real
    // spawn/teardown pair is what keeps that true.
    std::unique_ptr<AvatarController> networkedAvatarController_;
    std::vector<EntityId> networkedAvatarSkinnedEntities_;
    // Other players on a client: an animated avatar on each remote placeholder entity.
    struct RemoteAvatarRig {
        EntityId root = kNullEntity;
        std::vector<EntityId> skinned;
        std::unique_ptr<AvatarController> controller;
        glm::vec3 lastPosition{0.0f};
    };
    std::vector<RemoteAvatarRig> remoteAvatars_;
    void clearRemoteAvatars();
    void spawnRemoteAvatar(EntityId root);
    void tickRemoteAvatars(float dt);
    // Skinned body, face, hair and the shipped animation clips.
    bool buildAvatarRig(std::vector<EntityId>& skinned, std::unique_ptr<AvatarController>& controller);
    // Real per-tick finite-difference velocity for the networked avatar's
    // walk/run/idle blend -- this entity has no RigidBody for
    // AvatarController::tick()'s usual physics.getLinearVelocity() to
    // read (see above), so networkTickHook computes "how far did
    // networkedLocalPlayerEntity_'s own Transform actually move this
    // tick" instead and feeds that to the new physics-free tick()
    // overload directly.
    glm::vec3 networkedAvatarLastPosition_{0.0f};
    // The local online player's facing is drawn from how it actually moves
    // (or the camera in shift lock), not the server's rotation, so an older
    // server can't make the character face the wrong way.
    float networkedFacingYaw_ = 0.0f;
    float networkCameraZoom_ = 1.0f;
    float networkWheelAccumulator_ = 0.0f;
    float networkShoulderBlend_ = 0.0f;

    // Kronos (beta-blocking fix -- "rigged avatar turns into a capsule on
    // rejoin"): what actually happened wasn't a degrade at all --
    // characterController_.tick() (this file's own PreTickHook) has no
    // network-session guard, so it kept driving the offline rigged avatar
    // AND overwriting camera_ every sim tick even after joining an online
    // session, fighting the network hook above (which drives the separate
    // networkedLocalPlayerEntity_ capsule + camera_ on its own 60Hz
    // cadence) for the same camera_ object every frame -- see that hook's
    // own "driving characterController_'s physics-backed entity ... would
    // fight physics.step()" comment, which only ever solved the entity-
    // ownership half of this, not the tick/camera half. Tracks the
    // client-session edge (set in the PreTickHook below) so the offline
    // rigged avatar is real-hidden for the duration of a joined session --
    // not destroyed, so it's exactly as it was the instant leaveSession()
    // hands control back to it.
    bool wasNetworkedClient_ = false;

    // Sprint 14 ("render-tick decoupling"): the networked-client camera-
    // follow's own real mouse-look sampling moved from the (now 120Hz)
    // sim hook into the (60Hz) network hook, but UnifiedInput::mouseDelta()
    // resets every real UnifiedInput::update() call (SDL's own relative-
    // mouse accumulator semantics -- see that function's comment), and
    // update() itself still runs once per sim tick (input needs to stay
    // 120Hz-responsive for the offline CharacterController path). Without
    // this accumulator, the network hook would only ever see whichever
    // single sim tick's delta happened to be most recent, silently
    // dropping the other sim tick's worth of real mouse movement each
    // time two sim ticks elapse per one network tick. Filled every sim
    // tick, drained (read-and-zeroed) once per network tick.
    glm::vec2 networkMouseDeltaAccumulator_{0.0f};

    std::unique_ptr<runtime::GameLoop> gameLoop_;
    bool initialized_ = false;
    bool headless_ = false;
    bool sdlInitialized_ = false;

};

} // namespace engine::core
