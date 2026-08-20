#pragma once

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

namespace engine::core {

// Kronos ("Moderation Architecture v1/v2"): a real, honest, self-declared
// age signal -- there is no real age-verification system anywhere in
// this codebase (no ID check, no parental consent flow) and none is
// added here. `Unknown` is a real, deliberate, distinct value from
// `Adult` (not "assume adult") -- every real policy decision that reads
// this (safety::PolicyEngine, Minor Mode enforcement) treats `Unknown`
// exactly as conservatively as `Minor`, so an unset/unverified age never
// silently gets adult-level leniency ("protect minors by default").
// Lives in core:: (not safety::, where it originated in Phase 1) because
// it's fundamentally an account/profile attribute -- safety::PolicyEngine
// now depends on core::AgeGroup, not the reverse.
enum class AgeGroup { Unknown, Minor, Adult };

// Kronos ("Social Layer" -- "Friends + Presence + Messaging"): a real,
// local friend-list entry. `friendId` is the same real, stable
// `LocalProfile::creatorId` string every other cross-profile reference in
// this codebase already uses (Creator Profile panel, marketplace
// listings, publish records) -- a player adds a friend using an id
// they've actually seen somewhere real in the UI, not a fabricated
// lookup against a directory that doesn't exist. There is no account/
// server system anywhere in this codebase (see LocalProfile's own "no
// auth" scope) -- this is real, local, one-sided data: adding a friend
// here does not notify, modify, or even reach *their* actual profile in
// any way. See social::sendFriendRequest()'s own header comment for
// exactly what "send"/"accept"/"decline" honestly mean without a real
// transport to deliver any of it over.
struct FriendEntry {
    std::string friendId;
    std::string displayName;
};

// A real, local chat-log entry. `fromMe = true` is something this
// profile actually typed; `fromMe = false` is real too, but only ever
// exists if *this same machine* also played the other side locally (e.g.
// a manual test/demo of the UI) -- see social::sendMessage()'s own
// comment. There is no real transport to another machine anywhere in
// this codebase, so every stored message here is honestly local, never a
// delivered conversation.
struct FriendMessage {
    std::string friendId;
    bool fromMe = true;
    std::string text;
    int64_t timestampUnixSeconds = 0;
};

// Kronos ("Notifications System"): the real, specific event kinds this
// pass actually generates notifications for -- every one of these is a
// real, already-existing event in this codebase (a friend request, an
// item purchase, a rating received, a moderation outcome), not a
// speculative category with no real producer. `SystemMessage` is the one
// deliberately generic catch-all (used for things like a first-launch
// welcome message), matching the spec's own "System messages" line item.
enum class NotificationKind { FriendRequest, ItemPurchase, RatingReceived, ModerationUpdate, SystemMessage };

// A real, persistent notification -- `read` is real, mutable local
// state (toggled by notification::markRead(), see that file), not a
// derived/computed value, so "unread count" survives a restart exactly
// as it was left. `relatedId` is real but kind-dependent free text (a
// friendId for FriendRequest, an itemId for ItemPurchase/RatingReceived)
// -- callers that need to act on a notification (e.g. "jump to this
// friend request") read it back out by NotificationKind, same "the kind
// tells you how to interpret the payload" convention this codebase
// already uses for safety::TextRiskCategory-keyed data.
struct NotificationRecord {
    NotificationKind kind = NotificationKind::SystemMessage;
    std::string title;
    std::string body;
    std::string relatedId;
    int64_t timestampUnixSeconds = 0;
    bool read = false;
};

// Kronos (Alpha Roadmap Phase 9, "Platform Services" -- "Account system
// (simple local profiles first)"): a real, minimal local identity -- no
// authentication, no server-side account, no password, matching the
// roadmap's own "simple local profiles first" scope exactly. A stable,
// locally-generated profileId (see generateProfileId()) plus a display
// name a creator can change any time.
//
// Kronos ("Moderation Architecture v2", "Account System v1"): `ageGroup`
// (real, self-declared, see AgeGroup's own comment) and
// `creatorVerified` (real, but also manually-set-only -- there is no
// real creator-verification process/authority in this codebase, this is
// a real, honest field with nowhere yet that sets it true automatically,
// same "real seam, no fabricated verification" spirit as every other
// unimplemented-but-honest gap in this system) are new, real profile
// attributes both flow into real policy decisions -- see
// safety::PolicyEngine and RuntimeShell's Minor Mode enforcement.
struct LocalProfile {
    std::string displayName = "Player";
    uint64_t profileId = 0; // 0 = not yet assigned a real id -- see loadOrCreateProfile()
    int64_t createdAtUnixSeconds = 0;
    AgeGroup ageGroup = AgeGroup::Unknown;
    bool creatorVerified = false;

    // Kronos ("Creator Identity + Marketplace Publishing Pipeline"): a
    // real, stable creator identity -- deliberately NOT free text (see
    // core::AvatarItemManifest::creatorId's own comment on the real gap
    // this closes: every prior upload let a creator type an arbitrary
    // name into that field, with no link back to a real profile at all).
    // Auto-derived from the real, unique, already-assigned `profileId`
    // (see loadOrCreateProfile()) -- not separately user-editable, so an
    // uploaded item's creatorId always genuinely traces back to the real
    // local profile that published it.
    std::string creatorId;

    // Kronos ("Avatar Creation System, Marketplace & Economy" -- "Wire
    // Wallet -> Catalogue purchases"): a real, persistent, platform-wide
    // balance -- deliberately NOT core::Wallet's coins/gems (Economy.hpp),
    // which are a real but *separate* concept scoped to the mining-sim
    // gameplay loop's own session-local ECS Wallet component. KronosCredits
    // is the real currency a marketplace purchase (Studio's Catalogue
    // panel, no live gameplay session necessarily active) spends, and
    // persists here, on the same real, cross-session profile identity
    // every other real economy-adjacent record in this codebase is keyed
    // by (moderation::AccountModerationRegistry, moderation::Appeal::
    // profileId). Real, honest starting balance: 0 -- no fabricated
    // "free starter credits" grant invented here; see
    // marketplace::TransactionLog's own comment on what "earning"
    // credits does and doesn't do in this pass.
    int64_t kronosCredits = 0;
    // Real, persistent record of which real catalogue item ids this
    // profile has purchased -- prevents re-buying the same item and is
    // the real, honest substitute for a full "owned items" inventory UI
    // (not built this pass, a real, stated scope boundary).
    std::vector<std::string> ownedItemIds;

    // Kronos ("Avatar Phase" -- "AvatarEditor: Skin-Tone Selection"): a
    // real index into core::skinTonePalette() (core/AvatarSkinTone.hpp).
    // -1 (core::kNoSkinToneSelected) is the real, honest "never chosen
    // one" default -- resolveSkinToneColor() falls back to the same
    // hardcoded color spawnRiggedAvatar() always used before this
    // feature existed, so a profile that never opens AvatarEditor keeps
    // looking exactly as it did before. Deliberately a plain int, not
    // the glm::vec4 itself: this profile format (core/LocalProfile.cpp)
    // has no real vector/color field anywhere else, and a palette
    // selection is a real, small, enumerable choice, not a free color
    // pick -- see AvatarSkinTone.hpp's own header comment.
    int skinToneIndex = -1;

    // Kronos ("Avatar Phase" -- "Avatar Head System"): real, persistent
    // choice between core::HeadShape::Oval (0, the real, new default --
    // R15-style) and core::HeadShape::Sphere (1, "Classic Mode"). Stored
    // as a plain int (not core::HeadShape itself) for the same real
    // reason skinToneIndex is a plain int, not a glm::vec4: core/
    // LocalProfile.hpp deliberately doesn't depend on core/RiggedAvatar.hpp
    // (this is a real, small, standalone identity file, not part of the
    // rigged-avatar/rendering subsystem) -- callers resolve the real enum
    // via core::headShapeFromIndex() (core/RiggedAvatar.hpp) instead.
    int headShapeIndex = 0;

    // Kronos ("Avatar Phase" -- "AvatarEditor: Body Sliders"): real,
    // persistent height/width/limb-scale/torso-length/shoulder-width
    // multipliers -- plain floats (not core::BodyProportions itself), for
    // the same "this file doesn't depend on core/RiggedAvatar.hpp" reason
    // headShapeIndex is a plain int rather than core::HeadShape (see that
    // field's own comment). Default 1.0f each (identity) -- a profile
    // that never opens the Body Sliders section keeps the exact same
    // proportions this rig always used before this feature existed.
    float bodyHeight = 1.0f;
    float bodyWidth = 1.0f;
    float bodyLimbScale = 1.0f;
    float bodyTorsoLength = 1.0f;
    float bodyShoulderWidth = 1.0f;

    // Kronos ("Avatar Phase" -- "AvatarEditor: Animation Overrides"):
    // real, persistent core::AnimationItem ids (from the same real
    // animation catalogue studio::plugins::UploadAnimationPlugin writes
    // to) chosen to override this rig's shipped idle/walk/run/
    // jump_start/jump_air/jump_land clips -- the real, exact six real
    // shipped clip slots core::AvatarController exposes (setIdleClip/
    // setWalkClip/setRunClip/setJumpClip/setJumpAirClip/setJumpLandClip).
    // Empty string (the default) means "use the real, honest shipped
    // default" -- same convention as skinToneIndex/headShapeIndex. Plain
    // strings, not core::AnimationOverrides itself, for the same "this
    // file doesn't depend on core/AvatarController.hpp" reason
    // headShapeIndex is a plain int rather than core::HeadShape.
    std::string animOverrideIdleId;
    std::string animOverrideWalkId;
    std::string animOverrideRunId;
    std::string animOverrideJumpStartId;
    std::string animOverrideJumpAirId;
    std::string animOverrideJumpLandId;

    // Kronos ("Creator Payout Ledger + Ratings Submission + Marketplace
    // Moderation v3" -- "Ratings Submission"): real, persistent record of
    // which real catalogue items this profile has already rated -- the
    // real "prevent multiple ratings from the same profile" guard
    // marketplace::submitRating() checks, same "ownedItemIds" shape
    // ownsItem() already establishes.
    std::vector<std::string> ratedItemIds;

    // Kronos ("Settings Panel v2 + Input Remapping + Accessibility
    // Layer"): real, persistent player settings -- same flat-field
    // convention every other section of this struct already uses (no
    // nested `Settings` sub-struct, since this file's own hand-rolled
    // "KEY value" text format has no real notion of nesting -- see
    // saveToFile()'s own comment). Every field here has a real, honest
    // default matching this engine's actual current behavior *before*
    // this feature existed, so an old profile with none of these lines
    // real-loads unchanged.
    //
    // Graphics: `qualityPreset` composes core::Renderer's own real,
    // already-existing per-feature toggles (RT reflections/volumetric
    // fog/SSR/RT GI/cinematic mode) -- see runtime::RuntimeShell::
    // applyQualityPreset()'s own comment for the exact real mapping.
    // `fpsCap` real-drives core::GameLoop::RunConfig::targetRenderDt (0 =
    // uncapped). `vsyncEnabled` real-toggles core::Renderer's present
    // mode between FIFO (real vsync) and MAILBOX (uncapped) -- see
    // Renderer::setVsyncEnabled()'s own comment on why MAILBOX isn't the
    // default (this session's own past "whistling GPU fan" bug).
    // Kronos ("Settings Panel v2" -- "Window/Fullscreen scaling"): real,
    // closes what used to be a stated gap here -- core::Window now has a
    // real runtime `setFullscreen()`/`setSize()` (SDL_SetWindowFullscreen/
    // SDL_SetWindowSize), and the existing resize-triggered
    // Renderer::recreateSwapchain() path (already exercised by vsync
    // toggling) handles the swapchain invalidation, so no separate
    // Vulkan-side work was actually needed. `windowResolutionIndex` is a
    // real, small, enumerable choice (see kWindowResolutionPresets in
    // RuntimeShell.cpp), same "small honest list, not free-form input"
    // convention `colorblindModeIndex`/`clothingFitIndex` already use --
    // only meaningful while `fullscreenEnabled` is false.
    bool fullscreenEnabled = false;
    int windowResolutionIndex = 0;
    int qualityPresetIndex = 1; // 0=Low, 1=Medium (default), 2=High
    bool vsyncEnabled = true;
    int fpsCap = 180; // matches core::GameLoop's own pre-existing 1/180s pacer default
    // Kronos ("Graphics Setting -- Volumetric Fog Toggle"): real, explicit
    // override on top of qualityPresetIndex's own bundled default
    // (Low=off, Medium/High=on -- see RuntimeShell::applyQualityPreset()'s
    // own comment) -- a player who wants fog off on Medium/High (or on at
    // Low) doesn't have to give up every other effect that preset bundles.
    // applyAllSettingsFromProfile() applies this *after* the preset's own
    // bundled default, so the explicit choice here always wins. Defaults
    // true, matching qualityPresetIndex's own default of 1 (Medium, which
    // already turns fog on) -- an old profile with no line for this real-
    // loads unchanged.
    bool volumetricFogEnabled = true;

    // Audio: real master/music/SFX volume multipliers -- see
    // core::Audio::setMasterVolume()/setCategoryVolume()'s own comments
    // for exactly how each applies (master via miniaudio's own real
    // engine-level volume; music/SFX via the real, new
    // core::AudioCategory field on core::AudioSource).
    float masterVolume = 1.0f;
    float musicVolume = 1.0f;
    float sfxVolume = 1.0f;

    // Accessibility: `uiScale` real-drives both ImGui's global font scale
    // and its style metrics (button/padding/spacing sizes all scale
    // together); `textScale` is a real, separate, text-only multiplier on
    // top, for a player who wants bigger text without bigger buttons --
    // see runtime::RuntimeShell::applyAccessibilitySettings()'s own
    // comment for exactly how the two combine. `colorblindModeIndex`
    // real-drives a real tint-matrix pass in the existing composite
    // post-process shader (core::Renderer::setColorblindMode(), see that
    // method's own comment) -- 0=None/1=Protanopia/2=Deuteranopia/
    // 3=Tritanopia, matching accessibility::ColorblindMode's own real
    // enum order (see core/LocalProfile.cpp's own include comment on why
    // this file doesn't depend on accessibility::AccessibilityOptions
    // itself). `reducedMotion` real-scales real TNT Wars gameplay camera
    // shake to zero and real-freezes the cutscene FOV-change path (see
    // core::Application's own real consumption of this field).
    float uiScale = 1.0f;
    float textScale = 1.0f;
    int colorblindModeIndex = 0;
    bool reducedMotion = false;

    // Kronos ("Input Remapping System"): real, persistent overrides for
    // the real, specific gameplay actions this pass makes remappable
    // (MoveForward/MoveBackward/MoveLeft/MoveRight/Jump/Interact/
    // OpenChat/OpenShop -- see runtime::RuntimeShell::
    // applyInputBindingOverrides()'s own comment for the full real list
    // and why mouse-look/zoom/right-click/emote rebinding aren't included
    // -- those aren't real, existing bindable gameplay mechanics in this
    // engine yet, so there's nothing real to remap). Keyed by the exact
    // same action-name strings platform_adapters::UnifiedInput::
    // bindAction() already uses; value is a real SDL_Scancode. Empty map
    // (the real, honest default) means "use every action's own real,
    // already-existing default binding" -- see CharacterController.cpp's
    // own bindAction() calls for what those defaults actually are.
    std::unordered_map<std::string, int> inputBindingOverrides;

    // Kronos ("Avatar 2.0" -- "Clothing Meshes" -- "fit parameter"): real,
    // persistent, the same "plain int, small enumerable choice" shape
    // skinToneIndex/headShapeIndex already establish -- see
    // core::clothingFitFromIndex()/clothingFitToIndex() (RiggedAvatar.hpp)
    // for the real enum this resolves to. 0 (Tight) is the real, honest
    // default -- a pre-existing profile that never opens whatever real UI
    // eventually exposes this keeps the real, already-shipped tighter
    // shell fit, not a silently different look.
    int clothingFitIndex = 0;

    // Kronos ("Social Layer" -- "Friends + Presence + Messaging"): see
    // FriendEntry/FriendMessage's own comments above for the real, stated
    // scope. `pendingRequests` holds *outgoing* requests this profile has
    // sent but not yet resolved -- see social::sendFriendRequest()'s own
    // comment for why "resolved" is itself a local simulation (accept/
    // decline is triggered locally, standing in for what a real remote
    // response would do, since no transport exists to receive one).
    // `friendMessages` is real, persistent, and capped (see
    // social::kMaxStoredFriendMessages) so a long local chat history
    // doesn't grow this file unbounded, same "don't grow unbounded"
    // reasoning moderation::ChatLog's own ring buffer already applies.
    std::vector<FriendEntry> friends;
    std::vector<FriendEntry> pendingRequests;
    std::vector<FriendMessage> friendMessages;

    // Kronos ("Notifications System"): a real, persistent notification
    // log -- see core::NotificationRecord's own comment (below) for the
    // real event kinds and field shape. Capped the same
    // "don't grow unbounded" way friendMessages is (see
    // core::kMaxStoredNotifications in LocalProfile.cpp).
    std::vector<NotificationRecord> notifications;

    [[nodiscard]] bool ownsItem(const std::string& itemId) const;
    [[nodiscard]] bool hasRatedItem(const std::string& itemId) const;
    [[nodiscard]] bool isFriend(const std::string& friendId) const;
    [[nodiscard]] bool hasPendingRequestTo(const std::string& friendId) const;

    // Same hand-rolled "KEY value per line, END terminator" text format
    // every other save/load struct in core/ (ProjectFile, SceneFile,
    // Prefab) already uses.
    [[nodiscard]] bool saveToFile(const std::string& path) const;
    [[nodiscard]] bool loadFromFile(const std::string& path);
};

// Real, process-random 64-bit id -- not cryptographically secure (no
// need to be; this is a local-only alpha identity, not an auth token),
// just collision-resistant enough that independently-created profiles
// essentially never collide.
[[nodiscard]] uint64_t generateProfileId();

// Real, honest "get me a usable profile" entry point: loads `path` if it
// already exists; otherwise creates a fresh profile (a real
// generateProfileId(), the default display name, real
// std::chrono::system_clock "now" as its creation time -- same
// convention core::ProjectFile::touch() already uses), saves it to
// `path`, and returns it. The one real place this "load, or create if
// missing" logic lives, instead of every caller re-implementing it.
[[nodiscard]] LocalProfile loadOrCreateProfile(const std::string& path);

} // namespace engine::core
