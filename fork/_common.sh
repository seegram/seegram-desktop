# Shared helpers, sourced by the other scripts in this directory.

UPSTREAM_URL="https://github.com/telegramdesktop/tdesktop.git"
UPSTREAM_BRANCH="dev"

# The upstream remote and rerere are clone-local state: cloning this
# repository carries over neither. Rather than expect everyone to read the
# README first, set both up on first use.
fork_ensure_setup() {
	if ! git remote get-url upstream >/dev/null 2>&1; then
		echo "==> adding missing 'upstream' remote -> $UPSTREAM_URL"
		git remote add upstream "$UPSTREAM_URL"
	fi
	if [ "$(git config --get rerere.enabled || true)" != "true" ]; then
		echo "==> enabling rerere for this clone"
		git config rerere.enabled true
		git config rerere.autoupdate true
	fi
	# The driver is copied into .git and referenced by absolute path: during a
	# rebase the working tree is upstream's while the branding commit is
	# replayed, so a script referenced inside the tree would not exist yet.
	local gitdir driver
	gitdir="$(git rev-parse --git-dir)"
	driver="$gitdir/fork-merge-version-h.sh"
	if [ -f fork/merge-version-h.sh ]; then
		cp fork/merge-version-h.sh "$driver"
		chmod +x "$driver"
	fi
	if [ "$(git config --get merge.forkversion.driver || true)" = "" ]; then
		echo "==> registering the version.h merge driver for this clone"
		git config merge.forkversion.name \
			"keep the fork's app names, take upstream's version"
	fi
	git config merge.forkversion.driver "'$driver' %O %A %B"
	# The binding lives in clone-local info/attributes rather than a tracked
	# .gitattributes on purpose: during a rebase the working tree is upstream's
	# when the branding commit is replayed, so a tracked file would not be
	# there yet and the driver would never run.
	local attrs; attrs="$(git rev-parse --git-dir)/info/attributes"
	if ! grep -qs 'merge=forkversion' "$attrs"; then
		echo "==> binding version.h to the merge driver for this clone"
		mkdir -p "$(dirname "$attrs")"
		echo 'Telegram/SourceFiles/core/version.h merge=forkversion' >> "$attrs"
	fi
}

# upstream/<branch> exists only after a fetch.
fork_ensure_upstream_ref() {
	if ! git rev-parse --verify --quiet "upstream/$UPSTREAM_BRANCH" >/dev/null; then
		echo "==> fetching upstream for the first time, this downloads a lot"
		git fetch upstream "$UPSTREAM_BRANCH"
	fi
}

# The fork's base is derived, never stored: a state file would itself have
# to be rebased along with the commit series.
fork_base() {
	git merge-base "$1" "upstream/$UPSTREAM_BRANCH"
}
