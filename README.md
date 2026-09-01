# seegram

telegram desktop with the parts that talk about you switched off.

a fork of [tdesktop](https://github.com/telegramdesktop/tdesktop). it follows
upstream by rebase rather than merge, so `git log` still reads as a short list
of things this fork does, and nothing else. `fork/list.sh` prints that list.

## ghost mode

four switches, not one, because they are four different trades:

**don't send read receipts.** the other side never learns you opened it. costs
you nothing and nobody can tell.

**don't send typing status.** same deal, quieter chats.

**don't send online status.** this one is visible. anybody who could see when
you were last online will watch you simply stop
appearing, so know that before you flip it.

**don't send upload progress.** an upload notification is what gives away that
a large file is already on its way.

they live in settings → seegram. there's also one switch at the foot of the
side menu that flips all four together, and it shows as on only while all four
are on, so a half-on ghost mode never claims more than it does.

the idea is [ayugram](https://github.com/AyuGram/AyuGramDesktop)'s, they did it
first for telegram desktop. the code here isn't theirs: it hooks into
tdesktop's own send paths so that upstream files keep one line each and
updating to a new upstream release stays a five minute job.

## downloads

[releases](https://github.com/seegram/seegram-desktop/releases) has the macos
arm64 disk image, the windows x64 installer and a portable zip. installed
copies update themselves afterwards, so those are only for a first install.

macos: the app isn't signed with an apple developer id. first open needs a
right click on seegram.app → open. double clicking it reports the app as
damaged, which it isn't. after that it starts normally. [kramz](https://youtu.be/zdlfTSg-kUQ)
recorded a walkthrough if the prompts go sideways.

windows: smartscreen shows an unknown publisher warning. more info → run
anyway.

## building

you need your own api credentials. don't ship telegram's, accounts using them
get banned. register a pair at [my.telegram.org](https://my.telegram.org),
then:

```
Telegram/configure.sh -D TDESKTOP_API_ID=... -D TDESKTOP_API_HASH=...
```

on macos:

```
xcodebuild -project out/Telegram.xcodeproj -target Telegram -configuration Release
```

everything else is upstream's and lives in [docs/](docs/): the toolchains, the
40 gb of prepared libraries, the per-platform setup.

## license

gplv3 with the openssl exception, same as upstream. see [LEGAL](LEGAL).
