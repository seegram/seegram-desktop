# Fork maintenance

This repository is a fork of [telegramdesktop/tdesktop](https://github.com/telegramdesktop/tdesktop)
that carries a small set of extra features and follows upstream releases.

## Model: rebase, never merge

`main` is always **`<an upstream commit>` + a short series of fork commits on top**.
Updating means replaying that series onto a newer upstream revision. Merging
upstream into the fork is deliberately not used: it makes `git log` unreadable,
turns every update into a growing pile of conflicts, and destroys the ability to
answer "what exactly does this fork change?".

    upstream/dev  ──o──o──o──o──o          (mirror, never committed to)
                            │
    main                    └──A──B──C     (fork commits, replayed on update)

Answer "what does the fork change?" at any time:

    fork/list.sh

## Updating to a new upstream revision

    fork/update-upstream.sh v7.2.0      # a release tag
    fork/update-upstream.sh             # upstream/dev tip

The script fetches upstream, derives the current base, backs the branch up,
rebases, re-pins submodules, and prints a `git range-diff` so you can confirm
every fork commit survived and changed only where expected.

`rerere` is enabled in this clone (`git config rerere.enabled true`), so a
conflict resolved once is replayed automatically on later updates. This is the
single biggest time saver in fork maintenance - do not disable it.

## Rules for fork commits

These exist to keep the rebase cheap. Breaking them is what turns a 5-minute
update into a lost afternoon.

1. **Own code lives in own files.** Everything new goes under a dedicated
   directory in its own namespace. Upstream files get one-line hooks that call
   into it, never blocks of logic. A one-line conflict is trivial; a 200-line
   inserted block is not.
2. **Own settings, own storage.** Do not add fields to upstream's
   `Core::Settings` - it has a hand-written binary serializer, so every upstream
   field addition would collide. Keep fork settings in a separate file/format.
3. **Never reformat or refactor upstream code.** Even a whitespace change makes
   a file conflict forever.
4. **One feature, one commit**, with a real commit message. During a rebase you
   fix commits one at a time; a 3000-line "WIP" commit cannot be fixed.
5. **Avoid touching submodules.** Most UI code lives in `desktop-app/lib_ui`,
   `lib_base` etc., pinned by commit. Patching one means forking it and running
   this same rebase workflow for that repository too. Solve it in `tdesktop`
   where at all possible.

## Things that must not be inherited from upstream

- **API credentials.** Never ship upstream's `api_id`/`api_hash`; accounts using
  them get banned. Register your own at <https://my.telegram.org> and pass them
  as `-D TDESKTOP_API_ID=... -D TDESKTOP_API_HASH=...` at configure time.
  See `docs/api_credentials.md`.
- **Branding.** "Telegram" and its logo are trademarks. A published fork needs
  its own name, icons, and application identity, otherwise it also collides with
  an official install on the same machine.
- **Auto-updates.** Upstream's updater verifies packages with a private RSA key
  we do not have. Either build with the updater disabled and distribute through
  releases, or run an update server with a fork-owned key pair.

## Licence

Upstream is GPLv3 with an OpenSSL exception (see `LEGAL`). A distributed fork
must stay open source under the same terms and keep the existing copyright
notices.
