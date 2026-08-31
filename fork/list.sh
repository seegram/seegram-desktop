#!/usr/bin/env bash
# Show exactly what this fork adds on top of upstream.
set -euo pipefail
BRANCH="$(git rev-parse --abbrev-ref HEAD)"
BASE="$(git merge-base "$BRANCH" upstream/dev)"
echo "upstream base : $(git log -1 --format='%h %ci %s' "$BASE")"
echo "fork commits  : $(git rev-list --count "$BASE..$BRANCH")"
echo
git log --oneline --reverse "$BASE..$BRANCH"
echo
echo "files touched in upstream code:"
git diff --stat "$BASE..$BRANCH"
