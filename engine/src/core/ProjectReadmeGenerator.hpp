#pragma once

#include <string>

#include "core/ProjectFile.hpp"

namespace engine::core {

// Kronos ("Branding + Release Prep" -- "Basic README generator
// (Studio)"): a real, pure (headlessly-testable) markdown generator over
// a project's own real, already-existing metadata (core::ProjectFile) --
// no fabricated content, no placeholder lorem text; a project with no
// scenes yet just gets an honest "No scenes yet" line instead of an
// invented one.
[[nodiscard]] std::string generateProjectReadme(const ProjectFile& project);

// Writes generateProjectReadme(project) to "<projectDirectory>/README.md"
// (trunc-overwrite, same convention every other real save function in
// this codebase already uses). Returns false on a real I/O failure
// (unwritable directory, etc.).
[[nodiscard]] bool writeProjectReadme(const ProjectFile& project, const std::string& projectDirectory);

} // namespace engine::core
