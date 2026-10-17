#!/usr/bin/env bash
# Release checklist, the parts a script can check. Run from the repo root: scripts/release-check.sh [last-tag]
# Exit code 0 = every check passed. It never publishes anything.
set -u
cd "$(dirname "$0")/.."
fail=0
ok()   { echo "ok    $1"; }
bad()  { echo "FAIL  $1"; fail=1; }

last_tag="${1:-$(git describe --tags --abbrev=0 2>/dev/null || true)}"
range="${last_tag:+$last_tag..}HEAD"

# 1. Working tree: no uncommitted changes to tracked files.
if [ -z "$(git status --porcelain --untracked-files=no)" ]; then ok "tracked files are committed"; else bad "uncommitted changes to tracked files"; fi

# 2. Secrets in what is about to ship.
if git diff "$range" -U0 | grep -E '^\+' | grep -Ei 'BEGIN (RSA |EC |OPENSSH )?PRIVATE KEY|AKIA[0-9A-Z]{16}|api[_-]?key *[:=] *["'"'"'][A-Za-z0-9_-]{16,}' >/dev/null; then
  bad "something that looks like a secret was added since ${last_tag:-the first commit}"
else ok "no secrets in the diff since ${last_tag:-the first commit}"; fi

# 3. The current user's real home folder must not be in tracked files (example paths like /home/creator are fine).
me="$(whoami)"
if git grep -Il "/home/$me/" -- . ':!docs' >/dev/null 2>&1; then
  bad "your home folder /home/$me/ is in tracked files (run: git grep -n /home/$me/)"
else ok "no personal home path in tracked files"; fi

# 4. One version number.
ver=$(sed -n '/^project(/,/)/p' engine/CMakeLists.txt | grep -oE 'VERSION [0-9]+\.[0-9]+\.[0-9]+' | head -1 | cut -d' ' -f2)
if [ -n "$ver" ]; then ok "version in engine/CMakeLists.txt is $ver"; else bad "no VERSION in engine/CMakeLists.txt"; fi

# 5. Tests, when a build exists.
if [ -x engine/build/tests/engine_tests ]; then
  res=$(cd engine && ./build/tests/engine_tests 2>&1 | grep -E "FAIL|checks passed")
  if echo "$res" | grep -q FAIL; then bad "tests: $res"; else ok "tests: $res"; fi
else echo "skip  no engine/build/tests/engine_tests (build first)"; fi

# 6. Reminder: release notes are written by hand (see docs/MIGRATION_GUIDE.md for API changes).
echo "note  write the release notes (scripts/changelog.sh gives a draft)"

exit $fail
