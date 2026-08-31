#!/usr/bin/env bash
#
# Build, sign and publish one fork release for the current platform.
#
# Usage:  fork/release.sh <counter> [--no-publish]
#
# The counter is the fork's build number within one upstream version: bump it
# to ship a build between two upstream releases, reset it to 1 after a rebase
# onto a newer upstream. See Telegram/SourceFiles/fork/build_counter.h.
#
# The signing key never leaves this machine. Everything that needs it happens
# here; the update server only ever receives an already-signed package, so a
# compromise there cannot produce an update a client would accept.
set -euo pipefail
cd "$(git rev-parse --show-toplevel)"

KEYS_DIR="${SEEGRAM_KEYS_DIR:-$HOME/seegram-update-keys}"
KEY_ID="${SEEGRAM_KEY_ID:-sg-2026a}"
SERVER_SSH_KEY="${SEEGRAM_SSH_KEY:-$HOME/.ssh/seegram_updates}"

# Deliberately without defaults: where the update server lives and which
# account reaches it are not facts a public repository should carry. Checked
# below rather than here, so --no-publish needs neither.
SERVER="${SEEGRAM_UPDATE_SERVER:-}"
SERVER_ROOT="${SEEGRAM_UPDATE_ROOT:-}"
BUILD_DIR="out/Release"

COUNTER="${1:-}"
PUBLISH=1
[ "${2:-}" = "--no-publish" ] && PUBLISH=0

if ! [[ "$COUNTER" =~ ^[0-9]+$ ]] || [ "$COUNTER" -lt 1 ]; then
	echo "[ERROR] usage: fork/release.sh <counter> [--no-publish]" >&2
	exit 1
fi

# ---------------------------------------------------------------- preflight

# The counter file is the one thing a release is allowed to have changed:
# this script writes it itself, so re-running must not trip over that.
DIRTY="$(git status --porcelain --untracked-files=no \
	| grep -v 'fork/build_counter\.h$' || true)"
if [ -n "$DIRTY" ]; then
	echo "$DIRTY" >&2
	echo "[ERROR] working tree is dirty - a release must be reproducible." >&2
	exit 1
fi
if [ ! -f "$KEYS_DIR/release-private.pem" ]; then
	echo "[ERROR] signing key not found at $KEYS_DIR/release-private.pem" >&2
	echo "        set SEEGRAM_KEYS_DIR if it lives elsewhere." >&2
	exit 1
fi
if [ "$PUBLISH" -eq 1 ]; then
	if [ -z "$SERVER" ]; then
		echo "[ERROR] set SEEGRAM_UPDATE_SERVER, e.g. user@host" >&2
		exit 1
	fi
	if [ -z "$SERVER_ROOT" ]; then
		echo "[ERROR] set SEEGRAM_UPDATE_ROOT, the served directory" >&2
		exit 1
	fi
fi

case "$(uname -s)" in
Darwin)
	PLATFORM_KEY="armac"
	PACK_ARCH="arm64"
	APP="SeeGram.app"
	;;
*)
	echo "[ERROR] unsupported platform: $(uname -s)" >&2
	echo "        Windows releases are built by fork/release.ps1." >&2
	exit 1
	;;
esac

BASE="$(sed -n 's/.*AppVersion = \([0-9]*\);.*/\1/p' \
	Telegram/SourceFiles/core/version.h)"
if [ -z "$BASE" ]; then
	echo "[ERROR] could not read AppVersion from core/version.h" >&2
	exit 1
fi
# The client compares (base << 32 | counter), see Core::RunningUpdateVersion.
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
	sed -i.bak "s/^#define SEEGRAM_BUILD_COUNTER .*/#define SEEGRAM_BUILD_COUNTER $COUNTER/" "$COUNTER_FILE"
	rm -f "$COUNTER_FILE.bak"
	echo "    remember to commit $COUNTER_FILE"
fi

echo "==> configuring"
Telegram/configure.sh \
	-D TDESKTOP_API_ID="${SEEGRAM_API_ID:?set SEEGRAM_API_ID}" \
	-D TDESKTOP_API_HASH="${SEEGRAM_API_HASH:?set SEEGRAM_API_HASH}" \
	-D DESKTOP_APP_DISABLE_AUTOUPDATE=OFF \
	-D DESKTOP_APP_MAC_ARCH=arm64 \
	-D DESKTOP_APP_SPECIAL_TARGET=mac \
	-D CMAKE_CXX_FLAGS=-DPACKER_DISABLE_PRIVATE \
	-D CMAKE_EXE_LINKER_FLAGS="-Wl,-S -Wl,-x" >/dev/null

echo "==> building the client and the packer"
BUILD_LOG="$(mktemp)"
if ! ( cd out && xcodebuild -project Telegram.xcodeproj \
		-target Telegram -target Packer -configuration Release build ) \
		>"$BUILD_LOG" 2>&1; then
	echo "[ERROR] build failed:" >&2
	grep -E "error:" "$BUILD_LOG" | head -20 >&2
	echo "        full log: $BUILD_LOG" >&2
	exit 1
fi
rm -f "$BUILD_LOG"

# --------------------------------------------------------------- pack, sign

ROOT="$PWD"
STAGE="$(mktemp -d)"
trap 'rm -rf "$STAGE"' EXIT
cp -R "$BUILD_DIR/$APP" "$STAGE/"

echo "==> packing and signing"
( cd "$STAGE" && "$ROOT/$BUILD_DIR/Packer" \
	-path "$APP" \
	-version "$BASE" \
	-counter "$COUNTER" \
	-arch "$PACK_ARCH" \
	-channel stable \
	-keys-loc "$ROOT/Telegram/Resources/update" \
	-local-key "$KEYS_DIR/release-private.pem" \
	-local-key-id "$KEY_ID" ) | tail -3

PACKAGE="$(find "$STAGE" -maxdepth 1 -type f -name 'td-update-*' | head -1)"
if [ -z "$PACKAGE" ]; then
	echo "[ERROR] the packer produced no package" >&2
	exit 1
fi
echo "    package: $(basename "$PACKAGE") ($(( $(stat -f %z "$PACKAGE") / 1048576 )) MB)"

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

# The feed is edited one platform at a time on purpose: rewriting the whole
# file risks taking every other platform down with a single bad line. The
# version has to be a JSON string - the client parses a numeric one through a
# double, and a 64 bit version is past the point where a double is exact.
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
#
# A second, separate channel: the updater serves people who already run
# SeeGram, this serves people installing it for the first time. Attach a
# portable archive rather than the .tdup, which is only meaningful to the
# updater.

VERSION_STR="$(sed -n 's/.*AppVersionStr = "\([^"]*\)".*/\1/p' \
	Telegram/SourceFiles/core/version.h)"
TAG="v$VERSION_STR-$COUNTER"
ARCHIVE="$STAGE/SeeGram-$VERSION_STR-build$COUNTER-macOS-arm64.zip"

# Named explicitly: the clone also has an "upstream" remote, and gh guesses
# from the remotes - it picked telegramdesktop/tdesktop and got a 404.
GH_REPO_SLUG="$(git remote get-url origin \
	| sed -E 's#.*github\.com[:/]([^/]+/[^/.]+)(\.git)?$#\1#')"

# ------------------------------------------------------------------- dmg
#
# The familiar drag-to-Applications disk image. Unsigned, like the app
# itself: a Developer ID is tied to a paid Apple account, and Gatekeeper
# refusing an unsigned download once is a distribution problem rather than a
# build one - hence the note in the release body.

DMG="$STAGE/SeeGram-$VERSION_STR-build$COUNTER-macOS-arm64.dmg"
echo "==> building the disk image"
DMG_STAGE="$STAGE/dmg"
mkdir -p "$DMG_STAGE"
cp -R "$BUILD_DIR/$APP" "$DMG_STAGE/"
ln -s /Applications "$DMG_STAGE/Applications"
hdiutil create -volname "SeeGram" -srcfolder "$DMG_STAGE" \
	-ov -format UDZO -quiet "$DMG"
echo "    $(basename "$DMG") ($(( $(stat -f %z "$DMG") / 1048576 )) MB)"

if command -v gh >/dev/null 2>&1; then
	echo "==> attaching builds to release $TAG in $GH_REPO_SLUG"
	( cd "$BUILD_DIR" && zip -qry "$ARCHIVE" "$APP" )
	if ! gh release view "$TAG" --repo "$GH_REPO_SLUG" >/dev/null 2>&1; then
		gh release create "$TAG" --repo "$GH_REPO_SLUG" \
			--title "SeeGram $VERSION_STR build $COUNTER" \
			--notes "Telegram Desktop $VERSION_STR, SeeGram build $COUNTER.

Installed copies update themselves; these downloads are for a first install.

**macOS:** the app is not signed with an Apple Developer ID, so the first
open needs a right-click on SeeGram.app and then Open - double-clicking
reports it as damaged. Once opened this way it starts normally afterwards.

**Windows:** SmartScreen shows an unknown-publisher prompt on the installer;
More info, then Run anyway." \
			>/dev/null
	fi
	gh release upload "$TAG" "$ARCHIVE" "$DMG" \
		--repo "$GH_REPO_SLUG" --clobber >/dev/null
	echo "    $(basename "$ARCHIVE")"
	echo "    $(basename "$DMG")"
else
	echo "==> gh not installed, skipping the GitHub release"
fi

echo
echo "==> released $BASE build $COUNTER for $PLATFORM_KEY"
echo "    clients on an older build pick it up within 8 hours, or at once"
echo "    through Settings - Advanced - Check for updates."
