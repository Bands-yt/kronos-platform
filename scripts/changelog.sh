#!/usr/bin/env bash
# Draft a changelog from commit messages since a tag. Run from the repo root: scripts/changelog.sh [since-tag]
# Groups by the first word of the commit message: Add/New -> Added, Fix -> Fixed, everything else -> Changed.
set -u
cd "$(dirname "$0")/.."
since="${1:-$(git describe --tags --abbrev=0 2>/dev/null || true)}"
range="${since:+$since..}HEAD"
added=""; fixed=""; changed=""
while IFS= read -r subject; do
  case "$subject" in
    Add*|New*) added+="- ${subject}"$'\n' ;;
    Fix*)      fixed+="- ${subject}"$'\n' ;;
    *)         changed+="- ${subject}"$'\n' ;;
  esac
done < <(git log --no-merges --format='%s' "$range")
echo "# Changes since ${since:-the beginning}"
[ -n "$added" ]   && printf '\n## Added\n%s' "$added"
[ -n "$fixed" ]   && printf '\n## Fixed\n%s' "$fixed"
[ -n "$changed" ] && printf '\n## Changed\n%s' "$changed"
exit 0
