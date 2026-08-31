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
}

# upstream/<branch> exists only after a fetch.
fork_ensure_upstream_ref() {
	if ! git rev-parse --verify --quiet "upstream/$UPSTREAM_BRANCH" >/dev/null; then
		echo "==> fetching upstream for the first time, this downloads a lot"
		git fetch upstream "$UPSTREAM_BRANCH" --tags
	fi
}

# The fork's base is derived, never stored: a state file would itself have
# to be rebased along with the commit series.
fork_base() {
	git merge-base "$1" "upstream/$UPSTREAM_BRANCH"
}
