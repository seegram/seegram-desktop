#!/usr/bin/env bash
# Show exactly what this fork adds on top of upstream.
set -euo pipefail
cd "$(git rev-parse --show-toplevel)"
. fork/_common.sh
fork_ensure_setup
fork_ensure_upstream_ref

BRANCH="$(git rev-parse --abbrev-ref HEAD)"
BASE="$(fork_base "$BRANCH")"

echo "branch        : $BRANCH"
echo "upstream base : $(git log -1 --format='%h %ci %s' "$BASE")"
echo "fork commits  : $(git rev-list --count "$BASE..$BRANCH")"
echo
git log --oneline --reverse "$BASE..$BRANCH"
echo
echo "files changed against upstream:"
git diff --stat "$BASE..$BRANCH"
