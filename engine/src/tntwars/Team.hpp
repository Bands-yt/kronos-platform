#pragma once

namespace engine::tntwars {

// Kronos roadmap Milestone 1 ("TNT Wars: Foundational Systems"): the real
// team identity this game has never had -- until now, "TeamA"/"TeamB" only
// existed as cosmetic base-color constants in MapLayout.cpp (see that
// file's own kTeamAColor/kTeamBColor), with no real per-player team
// assignment or team-scoped game state anywhere. This is the real,
// minimal, two-team model TntWarsMatch's own player-team map and Core
// health (see TntWarsMatch.hpp) are keyed by.
//
// Deliberately just two teams, not an N-team roster -- every real mechanic
// this brief describes (a wall separating two sides, a Core per side,
// missiles/interception between two opposing bases) is explicitly
// two-sided; a general N-team model would be real, unused generality this
// engine doesn't need yet.
enum class TeamId { A, B };

[[nodiscard]] const char* teamName(TeamId team);

// The real opposing team -- used by the win-condition check (Team A's
// Core destroyed -> Team B wins) and anywhere else "the other side" needs
// a real, direct answer instead of an if/else at every call site.
[[nodiscard]] TeamId opposingTeam(TeamId team);

} // namespace engine::tntwars
