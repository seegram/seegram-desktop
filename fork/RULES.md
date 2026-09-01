# fork rules

These keep the rebase cheap. Breaking them turns a five minute update into a
lost afternoon.

**Rebase, never merge.** `main` is an upstream commit plus a short series of
fork commits on top. Merging upstream in makes `git log` unreadable and
destroys the answer to "what exactly does this fork change?". `rerere` replays
conflicts you have already resolved once, so do not disable it.

**Own code in own files.** Everything new goes under
`Telegram/SourceFiles/fork/` in its own namespace. Upstream files get one-line
hooks that call into it, never blocks of logic. A one-line conflict is trivial,
a 200-line inserted block is not.

**Never add fields to `Core::Settings`.** It serializes by hand, field after
field, so a field added there collides with every upstream release that adds
one. Fork settings live in their own file and their own format.

**Never reformat or refactor upstream code.** Even a whitespace change makes
that file conflict forever.

**One feature, one commit**, with a real message. During a rebase you fix
commits one at a time. A 3000-line WIP commit cannot be fixed.

**Avoid touching submodules.** Most UI code lives in `lib_ui`, `lib_base` and
friends, pinned by commit. Patching one means forking it and running this same
workflow for that repository too. Solve it in `tdesktop` where you can.

`fork/list.sh` prints the current fork commits, `fork/update-upstream.sh`
replays them onto a newer upstream revision.
