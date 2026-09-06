#!/usr/bin/env bash
# Kronos standalone-app smoke test: launches every real, currently-built
# app binary (Kronos/engine_runtime, Studio, 3D Maker, Movie Maker,
# Audio) for a few real seconds each and checks that none of them
# crashed -- the same real evidence this session's own manual checks
# already used (a clean timeout-kill exit code, plus an empty/absent
# core::CrashReporter crash_report_*.txt, which is only ever non-empty
# after a real SIGSEGV/SIGABRT/SIGFPE -- see engine/src/core/
# CrashReporter.cpp's own installCrashReporter()), turned into a real,
# reusable, checked-in script instead of one-off shell commands.
#
# Usage: scripts/verify_standalone_apps.sh [build-dir] [seconds-per-app]
#   build-dir       -- an already-configured-and-built CMake build
#                      directory (default: engine/build)
#   seconds-per-app -- how long to let each binary run before checking
#                      (default: 8)
set -euo pipefail

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD_DIR="${1:-${REPO_ROOT}/engine/build}"
DURATION="${2:-8}"
BIN_DIR="${BUILD_DIR}/src"

# label:binary -- covers all 6 real executables ENGINE_BUILD_RUNTIME/
# ENGINE_BUILD_STUDIO produce; `studio` (the legacy full-editor shell)
# is included alongside `kronos_studio` (the standalone Full-mode app)
# since both are real, independently launchable binaries today.
APPS=(
    "Kronos (Player):engine_runtime"
    "Studio (legacy):studio"
    "Studio:kronos_studio"
    "3D Maker:kronos_3d_maker"
    "Movie Maker:kronos_movie_maker"
    "Audio:kronos_audio"
)

fail=0
for entry in "${APPS[@]}"; do
    label="${entry%%:*}"
    binary="${entry##*:}"
    path="${BIN_DIR}/${binary}"

    if [ ! -x "$path" ]; then
        echo "SKIP  ${label} (${binary}) -- not built at ${path}"
        continue
    fi

    # Real, per-run crash report scan -- see this script's own header
    # comment for why an empty/absent file means "no real crash" and a
    # non-empty one means a real signal was actually caught.
    rm -f "${REPO_ROOT}/engine"/crash_report_*.txt

    set +e
    timeout "${DURATION}" "$path" > /tmp/kronos_verify_"${binary}".log 2>&1
    exit_code=$?
    set -e

    # timeout's own real exit code: 124 means the process was still
    # running and got killed by the timeout (the real, expected outcome
    # for a GUI app with no --headless/self-quit flag) -- 0 would mean
    # it exited on its own before the timer, also fine. Anything else is
    # a real, non-zero, non-timeout exit: a real crash or startup
    # failure.
    crashed_cleanly=0
    if [ "$exit_code" -ne 0 ] && [ "$exit_code" -ne 124 ]; then
        crashed_cleanly=1
    fi

    crash_report=""
    for f in "${REPO_ROOT}/engine"/crash_report_*.txt; do
        if [ -s "$f" ]; then
            crash_report="$f"
            break
        fi
    done

    if [ "$crashed_cleanly" -eq 1 ] || [ -n "$crash_report" ]; then
        echo "FAIL  ${label} (${binary}) -- exit=${exit_code} crash_report=${crash_report:-none} (see /tmp/kronos_verify_${binary}.log)"
        fail=1
    else
        echo "PASS  ${label} (${binary}) -- ran ${DURATION}s, exit=${exit_code}, no crash report"
    fi
    rm -f "${REPO_ROOT}/engine"/crash_report_*.txt
done

if [ "$fail" -ne 0 ]; then
    echo ""
    echo "verify_standalone_apps.sh: one or more apps FAILED -- see the logs named above."
    exit 1
fi

echo ""
echo "verify_standalone_apps.sh: all built apps ran clean."
