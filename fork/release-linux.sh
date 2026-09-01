#!/usr/bin/env bash
#
# Build, sign and publish one fork release for Linux.
#
# Usage:  fork/release-linux.sh <counter> [--no-publish]
#
# The sibling of fork/release.sh, which does the same for macOS. Linux is a
# separate script rather than another branch of that one because the build is
# the part that differs most: it happens inside upstream's rockylinux
# container, so that the binary runs on distributions older than the one it
# was built on. Everything after the build is deliberately the same shape.
#
# The image is built once, out of band:
#
#     cd Telegram/build/docker/centos_env
#     DEBUG= LTO= python gen_dockerfile.py > /tmp/Dockerfile.gen
#     docker build -f /tmp/Dockerfile.gen -t tdesktop:centos_env .
#
# Note the generated file goes somewhere else: Dockerfile in that directory is
# the jinja template it is generated FROM, and redirecting onto it truncates
# the template before the generator reads it.
set -euo pipefail
cd "$(git rev-parse --show-toplevel)"

KEYS_DIR="${SEEGRAM_KEYS_DIR:-$HOME/seegram-update-keys}"
KEY_ID="${SEEGRAM_KEY_ID:-sg-2026a}"
SERVER_SSH_KEY="${SEEGRAM_SSH_KEY:-$HOME/.ssh/seegram_updates}"
IMAGE="${SEEGRAM_LINUX_IMAGE:-tdesktop:centos_env}"

# Half a 32 core box, because the runner may well share the machine with
# something that has users waiting on it. A systemd limit on the runner
# service would not do this: docker runs the build in the daemon's own cgroup
# slice, not in the runner's, so the cap has to be on the container.
CPUS="${SEEGRAM_LINUX_CPUS:-16}"

SERVER="${SEEGRAM_UPDATE_SERVER:-}"
SERVER_ROOT="${SEEGRAM_UPDATE_ROOT:-}"
BUILD_DIR="out/Release"

PLATFORM_KEY="linux"
# The packer only accepts arm64 or x86_64 and ignores the value on Linux,
# where it hardcodes the target - passed anyway so the call reads the same
# as the other platforms'.
PACK_ARCH="x86_64"

COUNTER="${1:-}"
PUBLISH=1
[ "${2:-}" = "--no-publish" ] && PUBLISH=0

if ! [[ "$COUNTER" =~ ^[0-9]+$ ]] || [ "$COUNTER" -lt 1 ]; then
	echo "[ERROR] usage: fork/release-linux.sh <counter> [--no-publish]" >&2
	exit 1
fi

# ---------------------------------------------------------------- preflight

DIRTY="$(git status --porcelain --untracked-files=no \
	| grep -v 'fork/build_counter\.h$' || true)"
if [ -n "$DIRTY" ]; then
	echo "$DIRTY" >&2
	echo "[ERROR] working tree is dirty - a release must be reproducible." >&2
	exit 1
fi
if [ ! -f "$KEYS_DIR/release-private.pem" ]; then
	echo "[ERROR] signing key not found at $KEYS_DIR/release-private.pem" >&2
	exit 1
fi
if ! docker image inspect "$IMAGE" >/dev/null 2>&1; then
	echo "[ERROR] docker image '$IMAGE' not found, see the header of this file" >&2
	exit 1
fi
if [ "$PUBLISH" -eq 1 ]; then
	[ -n "$SERVER" ] || { echo "[ERROR] set SEEGRAM_UPDATE_SERVER" >&2; exit 1; }
	[ -n "$SERVER_ROOT" ] || { echo "[ERROR] set SEEGRAM_UPDATE_ROOT" >&2; exit 1; }
fi

BASE="$(sed -n 's/.*AppVersion = \([0-9]*\);.*/\1/p' \
	Telegram/SourceFiles/core/version.h)"
[ -n "$BASE" ] || { echo "[ERROR] no AppVersion in core/version.h" >&2; exit 1; }
VERSION="$(( (BASE << 32) | COUNTER ))"

echo "==> upstream version : $BASE"
echo "    fork build       : $COUNTER"
echo "    update version   : $VERSION"
echo "    platform         : $PLATFORM_KEY"

# ------------------------------------------------------------------- build

COUNTER_FILE="Telegram/SourceFiles/fork/build_counter.h"
CURRENT="$(sed -n 's/^#define SEEGRAM_BUILD_COUNTER \([0-9]*\).*/\1/p' "$COUNTER_FILE")"
if [ "$CURRENT" != "$COUNTER" ]; then
	echo "==> setting the build counter to $COUNTER"
	sed -i "s/^#define SEEGRAM_BUILD_COUNTER .*/#define SEEGRAM_BUILD_COUNTER $COUNTER/" "$COUNTER_FILE"
	echo "    remember to commit $COUNTER_FILE"
fi

# -u so that everything the build writes into the tree stays owned by the
# invoking user; a root-owned out/ is a mess to clear afterwards and would
# break the next run's git status check.
echo "==> building in $IMAGE"
BUILD_LOG="$(mktemp)"
if ! docker run --rm \
		--cpus "$CPUS" \
		-u "$(id -u):$(id -g)" \
		-v "$PWD:/usr/src/tdesktop" \
		"$IMAGE" \
		/usr/src/tdesktop/Telegram/build/docker/centos_env/build.sh \
		-D DESKTOP_APP_DISABLE_AUTOUPDATE=OFF \
		-D DESKTOP_APP_SPECIAL_TARGET=linux \
		-D CMAKE_CXX_FLAGS=-DPACKER_DISABLE_PRIVATE \
		>"$BUILD_LOG" 2>&1; then
	echo "[ERROR] build failed:" >&2
	grep -E "error:|Error" "$BUILD_LOG" | head -20 >&2
	echo "        full log: $BUILD_LOG" >&2
	exit 1
fi
rm -f "$BUILD_LOG"

# The executable is named from AppFile in core/version.h, so it follows the
# fork's branding rather than being called Telegram - found rather than
# assumed, so that renaming the app does not silently break the release.
BINARY="$(sed -n 's/.*AppFile = "\([^"]*\)".*/\1/p' \
	Telegram/SourceFiles/core/version.h)"
for f in "$BINARY" Updater Packer; do
	if [ ! -f "$BUILD_DIR/$f" ]; then
		echo "[ERROR] $BUILD_DIR/$f missing after the build" >&2
		exit 1
	fi
done

# --------------------------------------------------------------- pack, sign

ROOT="$PWD"
STAGE="$(mktemp -d)"
trap 'rm -rf "$STAGE"' EXIT

# What the updater replaces on disk: the client and the updater itself, in a
# directory of that name, which is the layout the updater unpacks into.
mkdir -p "$STAGE/$BINARY"
cp "$BUILD_DIR/$BINARY" "$BUILD_DIR/Updater" "$STAGE/$BINARY/"

# The packer runs in the container too. It is linked against the rockylinux
# runtime it was built on, and the point of building there is not to depend on
# the host's.
echo "==> packing and signing"
docker run --rm \
	-u "$(id -u):$(id -g)" \
	-v "$ROOT:/usr/src/tdesktop" \
	-v "$STAGE:/stage" \
	-v "$KEYS_DIR:/keys:ro" \
	-w /stage \
	"$IMAGE" \
	"/usr/src/tdesktop/$BUILD_DIR/Packer" \
	-path "$BINARY" \
	-version "$BASE" \
	-counter "$COUNTER" \
	-arch "$PACK_ARCH" \
	-channel stable \
	-keys-loc /usr/src/tdesktop/Telegram/Resources/update \
	-local-key /keys/release-private.pem \
	-local-key-id "$KEY_ID" | tail -3

PACKAGE="$(find "$STAGE" -maxdepth 1 -type f -name 'td-update-*' | head -1)"
if [ -z "$PACKAGE" ]; then
	echo "[ERROR] the packer produced no package" >&2
	exit 1
fi
echo "    package: $(basename "$PACKAGE") ($(( $(stat -c %s "$PACKAGE") / 1048576 )) MB)"

if [ "$PUBLISH" -eq 0 ]; then
	OUT="$ROOT/seegram-$VERSION-$PLATFORM_KEY.tdup"
	cp "$PACKAGE" "$OUT"
	echo "==> not publishing, package left at $OUT"
	exit 0
fi

# ----------------------------------------------------------------- publish

REMOTE_NAME="seegram-$VERSION-$PLATFORM_KEY.tdup"
echo "==> uploading $REMOTE_NAME"
scp -q -i "$SERVER_SSH_KEY" "$PACKAGE" "$SERVER:$SERVER_ROOT/packages/$REMOTE_NAME"

echo "==> updating the feed entry for $PLATFORM_KEY"
ssh -i "$SERVER_SSH_KEY" "$SERVER" \
	"SEEGRAM_PLATFORM='$PLATFORM_KEY' SEEGRAM_VERSION='$VERSION' python3 - <<'PY'
import json, os, shutil
root = '$SERVER_ROOT'
path = root + '/current4'
platform = os.environ['SEEGRAM_PLATFORM']
with open(path) as f:
    feed = json.load(f)
entry = feed.setdefault(platform, {}).setdefault('stable', {})
entry['released'] = os.environ['SEEGRAM_VERSION']
entry.setdefault(
    'link', '/packages/seegram-{version}-' + platform + '.tdup')
tmp = path + '.new'
with open(tmp, 'w') as f:
    json.dump(feed, f, indent=2)
    f.write('\n')
os.replace(tmp, path)
shutil.copyfile(path, root + '/current')
for p in (path, root + '/current'):
    shutil.chown(p, 'www-data', 'www-data')
print('feed updated for ' + platform)
PY"

echo "==> verifying what clients will actually see"
FEED_VERSION="$(curl -fsS https://desktop.see.tg/current4 | python3 -c \
	"import sys, json; print(json.load(sys.stdin)['$PLATFORM_KEY']['stable']['released'])")"
if [ "$FEED_VERSION" != "$VERSION" ]; then
	echo "[ERROR] the feed says $FEED_VERSION, expected $VERSION" >&2
	exit 1
fi
HTTP="$(curl -fsS -o /dev/null -w '%{http_code}' -I \
	"https://desktop.see.tg/packages/$REMOTE_NAME")"
if [ "$HTTP" != "200" ]; then
	echo "[ERROR] the package is not reachable, HTTP $HTTP" >&2
	exit 1
fi

# --------------------------------------------------------- github release

VERSION_STR="$(sed -n 's/.*AppVersionStr = "\([^"]*\)".*/\1/p' \
	Telegram/SourceFiles/core/version.h)"
TAG="v$VERSION_STR-$COUNTER"
ARCHIVE="$STAGE/SeeGram-$VERSION_STR-build$COUNTER-Linux-x64.tar.xz"

GH_REPO_SLUG="$(git remote get-url origin \
	| sed -E 's#.*github\.com[:/]([^/]+/[^/.]+)(\.git)?$#\1#')"

echo "==> building the tarball"
tar -C "$STAGE" -cJf "$ARCHIVE" "$BINARY"
echo "    $(basename "$ARCHIVE") ($(( $(stat -c %s "$ARCHIVE") / 1048576 )) MB)"

if command -v gh >/dev/null 2>&1; then
	echo "==> attaching the build to release $TAG in $GH_REPO_SLUG"
	if ! gh release view "$TAG" --repo "$GH_REPO_SLUG" >/dev/null 2>&1; then
		echo "[ERROR] release $TAG does not exist yet." >&2
		echo "        the macOS job creates it; nothing to attach to." >&2
		exit 1
	fi
	gh release upload "$TAG" \
		"$ARCHIVE#Linux 64 bit: Binary" \
		--repo "$GH_REPO_SLUG" --clobber >/dev/null
	echo "    $(basename "$ARCHIVE")"
else
	echo "==> gh not installed, skipping the GitHub release"
fi
