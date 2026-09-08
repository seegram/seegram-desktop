# SeeGram development

SeeGram follows Telegram Desktop's build system and platform documentation.
Fork features live in `Telegram/SourceFiles/fork/`; this directory contains
build configuration, release scripts and upstream maintenance tools.

## Build setup

Start with the upstream instructions for [Windows](../docs/building-win.md),
[macOS](../docs/building-mac.md) or [Linux](../docs/building-linux.md).
Keep the checkout next to its prepared `Libraries` directory.

Register your application at [my.telegram.org](https://my.telegram.org).
The standard `TDESKTOP_API_ID` and `TDESKTOP_API_HASH` CMake options work as
in Telegram Desktop. No production credentials are included in this repository.
`TDESKTOP_API_TEST=ON` selects upstream's limited test credentials for testing.

For a private local default, create `fork/private/api_credentials.cmake`:

```cmake
set(TDESKTOP_API_ID YOUR_API_ID CACHE STRING "Telegram API ID" FORCE)
set(TDESKTOP_API_HASH YOUR_API_HASH CACHE STRING "Telegram API hash" FORCE)
```

`fork/private/` is ignored by Git. The loader uses these sources in order:

1. `TDESKTOP_API_ID` and `TDESKTOP_API_HASH` environment variables, supplied together.
2. The private local file above.
3. The usual CMake `-D` options or existing cache.

Environment variables and the local file replace cached credentials. Reconfigure
an existing build after changing them. For an existing macOS `out/` tree:

```sh
env -u QT cmake -S . -B out
cmake --build out --config Release --target Telegram
```

## Version numbers

`Telegram/build/version` and `core/version.h` follow the upstream version.
`Telegram/SourceFiles/fork/build_counter.h` holds the SeeGram release counter.
For example, `7.2.5 (build 1)` is the first SeeGram release based on Telegram
Desktop 7.2.5. Increment the counter for a new SeeGram release on the same
base; reset it to 1 when moving to a new upstream version. Local rebuilds do
not increment it.

Commit a counter change before releasing. Release scripts require a clean
tracked tree and an input counter matching the header; they never rewrite it.
The client and update packages compare `(upstream_version << 32) | counter`,
so a newer upstream version sorts after all builds of the previous version.

## Releases

The manually triggered [SeeGram Release workflow](../.github/workflows/seegram-release.yml)
builds on the project's prepared macOS, Windows and Linux runners.
Set the repository's GitHub Actions secrets `TDESKTOP_API_ID` and
`TDESKTOP_API_HASH` before running it. The workflow requires both and forwards
them to the build, including the Linux container.

The platform entry points are `fork/release.sh`, `fork/release.ps1` and
`fork/release-linux.sh`. Each accepts a fork build counter and an option to
build without publishing; usage is documented at the top of each script.
The [release bot](bot/README.md) previews the version, prepares the committed
counter and runs a pinned commit. Prefer `/release` for automatic numbering.
Windows releases target x64 only; x86 and ARM64 are not served.
Windows release builds scan both executable files with updated Microsoft
Defender signatures before running the updater test or publishing. A detection,
scan failure, or unavailable Defender blocks publication. The scan does not
change antivirus exclusions or restore quarantined files. A successful scan
records a point-in-time result; it is not a guarantee against later detections.
The manually triggered `Check Windows updater` workflow builds an isolated
Release updater, checks its product information, scans it, and tests replacement
and restart without publishing a release.
The Windows packer receives explicit root files, never `-path .`; the publisher
verifies the signed file table contains `SeeGram.exe` and `Updater.exe` at its
root before updating the feed.

Every release includes the shared [installation guide](release-installation.md).
Both macOS and Windows release creators read this file, so the instructions
and the credited community video are present regardless of which job finishes
first. Keep this guide in release descriptions when editing them manually.

Signing and deployment use the separate `SEEGRAM_*` settings named in the
workflow. Private signing keys remain on the runners.

Update controls live under Settings → SeeGram → Updates. The startup check is
enabled by default and saved in the application's preferences; it bypasses the
normal check interval once per process when automatic updates are enabled.
`fork/settings_updates.h` keeps beta controls hidden with `kShowBetaOptions`.

## Profile badges

The see.tg integration reads profile verifications from the same backend as
@seetgbot. Badge artwork follows the miniapp; `slot` controls which side of
the name displays each badge. Badges use the native verification size and theme colors. Descriptions appear
as icon-and-text rows in the lower verification block, alongside Telegram
descriptions, with inline formatting and links. Telegram verification,
Premium and bot verification remain independent.

The implementation lives in `Telegram/SourceFiles/fork/seetg/seetg_verifications.*`;
SVG resources live in `Telegram/Resources/fork/verifications/`. Requests use
the existing account-scoped see.tg cache and follow the integration toggle.

`giftchanges.svg` keeps the miniapp's original colored artwork. Its matching
path/gradient masks are expressed as gradient opacity stops: Qt's SVG renderer
otherwise drops the cat's head, legs and tail. Preserve these opacity gradients
when updating the artwork; importing the browser SVG unchanged restores the bug.

Descriptions use `descriptionTranslations` from the backend, choosing the
client’s SeeGram language, then English, then the original `description`.
The miniapp selects its own UI language independently. Telegram-issued
verification descriptions remain the text provided by Telegram.

All 30 SeeGram dictionaries live in `Telegram/Resources/fork/langs/`.
`fork_lang.cpp` lists the language metadata and stable string keys. Run
`python3 fork/check_locales.py` after editing dictionaries; it checks every
key, language resource and interpolation token.

## Upstream updates

The fork is maintained as commits on top of Telegram Desktop. Keep changes
focused on SeeGram features and their necessary integration points; preserve
upstream code, submodules and platform documentation where possible.

- `fork/list.sh` lists fork commits.
- `fork/update-upstream.sh [upstream-ref]` rebases them onto an upstream revision.
- [RULES.md](RULES.md) describes the maintenance conventions.
- [AGENTS.md](../AGENTS.md) contains repository instructions for coding agents.

## Update source

Stable SeeGram builds use `https://desktop.see.tg/current4` exclusively.
Telegram's `autoupdate_url_prefix` config and a migrated `tdata/prefix` cannot
redirect them to Telegram's update service. Stable builds do not consult
Telegram's MTP update channel: an unavailable SeeGram feed must be shown as
an error, not as "latest" based on Telegram's unrelated version list.

Older builds that accepted Telegram's update prefix may need one manual
installation of a fixed SeeGram release. Their data folder should be preserved.

## Ghost and spy modes

Ghost mode includes story read receipts and profile-story view counters.
Its five privacy switches default to off. Existing installations with all four
older switches enabled also enable the new story switch. Scheduled sending is
separate, defaults to off, and is preserved when the master switch changes.
It schedules ordinary sends 12 seconds ahead (15 with a configured proxy),
refreshing the deadline after uploads. Explicit schedules, bot destinations,
business templates, edits, suggested posts and outgoing timed media retain their
normal send behavior. Automatic scheduling does not open the scheduled-message
section. This follows [AyuGram's ghost scheduling](https://github.com/AyuGram/AyuGramDesktop/blob/dev/Telegram/SourceFiles/ayu/utils/telegram_helpers.cpp).

Spy mode previews incoming unread timed photos, videos, voice messages and round
videos by default, without automatically sending a content-read receipt or
burning single-view media on close. The message menu's **View** action explicitly
sends the receipt; only a successful response marks the message viewed and
starts its original timer. Server expiry still applies. Turning the setting off
restores the normal media presentation, including in already open chats.

Automatic ghost scheduling shows a local pending bubble in the chat. Its clock
opens the actual scheduled message; only that server-owned message can be
edited, sent now or cancelled. The bubble is removed when the scheduled item
is sent or deleted, and ordinary scheduled messages are not mirrored.
Self-destructing media previews retain a visible timer or single-view badge;
drawing the badge does not acknowledge the media.

When spy media previews are enabled, fully loaded expiring media keeps a local
reference for the lifetime of its message in this session. The expired service
bubble offers an inline Open link. It opens the retained media in the normal
viewer without another read acknowledgement. Media that was never downloaded
is not recovered, and disabling the setting hides the link immediately.

Ghost auto-scheduling has an independent “Disable notifications from yourself”
option, enabled by default. Only message IDs linked to SeeGram's automatic
scheduling are filtered; manual schedules and incoming messages keep Telegram's
notification rules. Per-account origin IDs are stored locally so reconnecting
or restarting does not turn manual schedules into ghost messages.
