# Release control bot

The owner uses `/release` (or the **New release** button) to preview the next
release. The bot reads the exact main commit, the committed counter and the
published feed. A new upstream base starts at the committed counter, normally
1; another release on that base increments the highest published counter.
`/release N` can select a higher number explicitly.

Confirmation expires after ten minutes. Changes to main, another active run,
an already published version or a rollback abort preparation. If needed, the
bot commits only `Telegram/SourceFiles/fork/build_counter.h` through GitHub's Git API with a
non-forced ref update. It creates an immutable `release-build/<request-id>`
tag and dispatches that ref. The unique request ID and commit identify the
run, so another person's run cannot be mistaken for this one.

`/status` shows platform steps. `/feed` checks public versions and package
availability. `/retry` repeats only failed jobs of a tracked release, preserving
its commit and version. JSON state survives service restarts; uncertain dispatch
requests are reconciled with GitHub, never automatically sent twice. If a crash
occurs between preparation and dispatch, inspect `/status` before trying again.

Supported releases: Windows x64, macOS arm64/x64, Linux x64. Windows x86 and
ARM64 are deliberately absent from the feed. Signing keys stay on the runners.

## Server deployment

Install `seegram_bot.py` and `release_core.py` together in `/opt/seegram-bot`.
The existing `seegram-bot.service` uses its virtualenv Python (aiogram 3 and
aiohttp). Configuration belongs in the service environment file, never Git:

- `BOT_TOKEN`, `OWNER_ID`: Telegram bot and its only permitted private-chat user.
- `GH_TOKEN`: repository Contents read/write and Actions read/write. It must
  be allowed to update main under the repository's branch rules.
- `REPO`: `seegram/seegram-desktop`.
- `WORKFLOW`: defaults to `seegram-release.yml`.
- `FEED_URL`: defaults to `https://desktop.see.tg/current4`.
- `STATE_FILE`: defaults to `release-state.json` in the working directory.

Back up the old source and state before installing. Deploy the matching workflow
before restarting the bot. Do not copy tokens or state into the repository.

Release scripts upload a staging package, then run `fork/publish_feed.py` on the
update server. It verifies the pinned root, stable-channel signature, payload,
architecture and version before publishing. A shared `flock` protects all
platforms; writes are atomic, downgrades and replacing an immutable package are
rejected. `/current` is a symlink to `/current4` for older clients.

Tests (Python with cryptography):

```sh
python3 -m unittest discover -s fork/tests -p test_release_controls.py -v
```
