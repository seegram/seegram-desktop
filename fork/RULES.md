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

**Nothing but `workflow_dispatch` reaches a self-hosted runner.** The release
runners are a personal Mac, a Windows box and a server that also runs a
database. A workflow on them executes as their user and can read the update
signing key and the deploy ssh key. `workflow_dispatch` needs write access to
this repository, so today only its owner can start one - `pull_request` needs
nothing at all, and on a public repository that is a stranger's code on your
machines. Upstream's own workflows all had such triggers, which is one reason
they are gone. Do not add one back.

`fork/list.sh` prints the current fork commits, `fork/update-upstream.sh`
replays them onto a newer upstream revision.
