#pragma once

#include "cinematic/Sequencer.hpp"
#include "core/ECS.hpp"

namespace engine::cinematic {

// Kronos ("Cinematic Sequencer Luau & TypeScript Bindings" -- Beta
// Roadmap): the real missing execution step Sequencer.hpp's own tracks
// otherwise have no consumer for -- Sequence::sampleChannel() is real
// and tested, but nothing before this file ever called it to actually
// move anything in a live scene. Deliberately free of ImGui/Vulkan/Lua,
// the same "pure logic, separate GPU/script-owning caller" split this
// codebase already establishes elsewhere (core::EditableMesh vs
// studio::plugins::ModelingModePlugin; this file vs
// studio::ScriptCinematicApi) -- headlessly testable with a bare
// core::ECS, no window/device needed.
//
// Real, honest scope: applies exactly two of TrackKind's six values --
// Transform and LightIntensity -- the two with an obvious, unambiguous
// real ECS target (core::Transform, core::Light). Camera is real too but
// applied differently, through CameraRail::sample() rather than a plain
// channel-to-component write (see studio::plugins::MovieModePlugin's own
// railParameterAtPlayhead()). SkeletalAnimation/Audio/ScriptTrigger
// tracks are real, storable, keyframeable/eventable data today (Sequence
// itself doesn't discriminate what a track's channels/events are for),
// but actually applying them needs a real RuntimeAnimationPlayer/
// core::Audio/event-dispatch call site this pure function has no access
// to -- real, separate, deferred scope, not silently dropped or faked.
//
// Channel naming convention (a track's own TrackChannel::name):
// Transform tracks read "position.x"/"position.y"/"position.z" and
// "rotation.x"/"rotation.y"/"rotation.z" (Euler degrees -- the same
// convention core::ScriptWorldApi::luaSetRotation() already uses, so a
// script author only has to learn one rotation convention across both
// bindings). Any one of those 6 channels missing from a track is left
// alone, not zeroed, so a track that only animates position doesn't
// reset rotation to identity every frame. LightIntensity tracks read a
// single channel named "intensity".
//
// A track that's muted, has targetId == 0 ("not bound yet", see
// SequencerTrack::targetId's own header comment), or whose targetId
// doesn't resolve to a live entity with the matching component, is a
// real, honest no-op for that track -- not an error, and every other
// track still applies normally.
void applySequenceToScene(const Sequence& sequence, float timeSeconds, core::ECS& ecs);

} // namespace engine::cinematic
