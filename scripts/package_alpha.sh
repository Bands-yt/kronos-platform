#!/usr/bin/env bash
# Kronos Alpha packaging script (Alpha Completion Checklist, "Alpha
# Packaging" -- "engine/studio binary packaging").
#
# Assembles a real, relocatable, flat-layout distribution directory --
# NOT just a copy of the build tree. This matters for a real reason: both
# engine_runtime and studio previously located their own shaders/assets
# via ENGINE_SHADER_DIR/ENGINE_ASSET_DIR, compile-time absolute paths
# baked in on the machine that built them, so copying those binaries
# anywhere else silently broke them (see core/ResourcePaths.hpp's own
# comment on the fix). The binaries now prefer a real "shaders"/"assets"
# directory sitting right next to themselves before ever falling back to
# that compile-time path -- this script is what actually produces that
# real, working, side-by-side layout, and is the thing that makes the fix
# meaningful rather than theoretical.
#
# Usage: scripts/package_alpha.sh [build-dir] [output-dir]
#   build-dir  -- an already-configured-and-built CMake build directory
#                 (default: engine/build)
#   output-dir -- where to assemble the real package (default: dist/kronos-alpha)
set -euo pipefail

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD_DIR="${1:-${REPO_ROOT}/engine/build}"
OUT_DIR="${2:-${REPO_ROOT}/dist/kronos-alpha}"

fail() { echo "package_alpha.sh: $*" >&2; exit 1; }

[ -x "${BUILD_DIR}/src/engine_runtime" ] || fail "engine_runtime not found/executable at ${BUILD_DIR}/src/engine_runtime -- build it first"
[ -x "${BUILD_DIR}/src/studio" ] || fail "studio not found/executable at ${BUILD_DIR}/src/studio -- build it first"
[ -d "${BUILD_DIR}/shaders" ] || fail "no compiled shaders at ${BUILD_DIR}/shaders -- build engine_shaders first"

echo "package_alpha.sh: assembling real package at ${OUT_DIR}"
rm -rf "${OUT_DIR}"
mkdir -p "${OUT_DIR}"

# Binaries -- flat, at the package root, so core::executableDirectory()
# (the real directory containing whichever of these actually ran) and the
# real "shaders"/"assets" siblings below resolve correctly for both.
cp "${BUILD_DIR}/src/engine_runtime" "${OUT_DIR}/"
cp "${BUILD_DIR}/src/studio" "${OUT_DIR}/"

# Real compiled shaders (.spv) -- copied, not symlinked, so the package
# is genuinely self-contained and movable off this machine.
mkdir -p "${OUT_DIR}/shaders"
cp "${BUILD_DIR}"/shaders/*.spv "${OUT_DIR}/shaders/"

# Real assets (procedurally-synthesized audio, UI texture atlas -- see
# ENGINE_ASSET_DIR's own CMakeLists.txt comment for what's actually in
# here and why none of it is a licensed/scraped asset).
cp -R "${REPO_ROOT}/engine/assets" "${OUT_DIR}/assets"

# Plugin folder structure (Alpha Completion Checklist, "plugin folder
# structure"): `plugins/` is the real, empty runtime scan directory
# studio::plugins::PluginBrowserPlugin defaults to (see its own
# pluginDirectoryBuffer_ = "plugins") -- present so a creator can drop a
# real plugin folder straight in without first having to create it
# themselves. `templates/plugin/` is the separate, real, working starting
# point (Section 4) a plugin developer copies FROM, not a plugin Studio
# would try to load on its own (it has no .manifest at this path level).
mkdir -p "${OUT_DIR}/plugins"
mkdir -p "${OUT_DIR}/templates"
cp -R "${REPO_ROOT}/templates/plugin" "${OUT_DIR}/templates/plugin"

# Default project template (Alpha Completion Checklist, "default project
# template"): a real, loadable starting project -- see
# templates/project/default.{project,scene}'s own generation comment.
cp -R "${REPO_ROOT}/templates/project" "${OUT_DIR}/templates/project"

# Real gameplay-Script creator examples (Section 5) -- useful reference
# material to ship alongside the binaries, not required for either
# binary to run.
if [ -d "${REPO_ROOT}/examples" ]; then
    cp -R "${REPO_ROOT}/examples" "${OUT_DIR}/examples"
fi

echo "package_alpha.sh: done. Real package layout:"
find "${OUT_DIR}" -maxdepth 2 | sed "s|${OUT_DIR}|  kronos-alpha|"
