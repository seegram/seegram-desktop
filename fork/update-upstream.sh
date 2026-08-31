#!/usr/bin/env bash
#
# Rebase the fork's commits onto a newer upstream revision.
#
# Usage:  fork/update-upstream.sh [upstream-ref]      (default: upstream/dev)
#         fork/update-upstream.sh v7.2.0
#
# The current upstream base is derived, not stored: it is the merge-base of the
# fork branch and upstream/dev. That keeps the fork free of any state file that
# would itself have to be rebased.
set -euo pipefail

TARGET="${1:-upstream/dev}"
BRANCH="$(git rev-parse --abbrev-ref HEAD)"

if [ "$BRANCH" = "HEAD" ]; then
	echo "[ERROR] detached HEAD - check out the fork branch first." >&2
	exit 1
fi
if [ -n "$(git status --porcelain --untracked-files=no)" ]; then
	echo "[ERROR] working tree is dirty - commit or stash first." >&2
	exit 1
fi

echo "==> fetching upstream"
git fetch upstream --tags --prune

BASE="$(git merge-base "$BRANCH" upstream/dev)"
NEW="$(git rev-parse "${TARGET}^{commit}")"
OLD_HEAD="$(git rev-parse "$BRANCH")"

echo "    branch    : $BRANCH"
echo "    old base  : $(git log -1 --format='%h %s' "$BASE")"
echo "    new base  : $(git log -1 --format='%h %s' "$NEW")"

if [ "$BASE" = "$NEW" ]; then
	echo "==> already based on $TARGET, nothing to do."
	exit 0
fi

COUNT="$(git rev-list --count "$BASE..$BRANCH")"
echo "==> replaying $COUNT fork commit(s)"
git log --oneline --reverse "$BASE..$BRANCH" | sed 's/^/    /'

BACKUP="backup/${BRANCH}-$(git log -1 --format=%cd --date=format:%Y%m%d-%H%M%S "$OLD_HEAD")"
git branch -f "$BACKUP" "$OLD_HEAD"
echo "==> backup branch: $BACKUP"

if ! git rebase --onto "$NEW" "$BASE" "$BRANCH"; then
	cat >&2 <<-MSG

	[CONFLICT] Rebase stopped. Resolve, then:
	    git add -A && git rebase --continue
	Or abort with:
	    git rebase --abort   (the branch is also saved at $BACKUP)

	rerere is enabled, so a conflict you resolve once is replayed
	automatically on the next update.
	MSG
	exit 1
fi

echo "==> syncing submodules to the new upstream pins"
git submodule update --init --recursive

echo
echo "==> range-diff (old series vs new series)"
git range-diff "$BASE..$OLD_HEAD" "$NEW..$BRANCH" || true

cat <<-MSG

	Done. Verify above that every fork commit survived and changed only where
	you expect. Then rebuild:
	    Telegram/configure.bat x64 -D TDESKTOP_API_ID=... -D TDESKTOP_API_HASH=...
MSG
