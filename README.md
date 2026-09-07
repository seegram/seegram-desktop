<p align="center">
  <img src="https://t.me/i/userpic/320/seetgbot.jpg" width="112" height="112" alt="SeeGram logo">
</p>

<h1 align="center">SeeGram Desktop</h1>

<p align="center">
  Telegram Desktop with ghost mode, message history and SeeTg integration.
</p>

<p align="center">
  <a href="https://github.com/seegram/seegram-desktop/releases">Downloads</a> ·
  <a href="https://t.me/seeclient">News</a> ·
  <a href="https://github.com/seegram/seegram-desktop/issues">Issues</a> ·
  <a href="fork/README.md">Building & development</a>
</p>

## Features

SeeGram is an independent fork of [Telegram Desktop](https://github.com/telegramdesktop/tdesktop).
Its extra controls live in **Settings → SeeGram**.

| Feature | What it adds |
| --- | --- |
| Ghost mode | Separate switches for read receipts, typing, online status and upload progress, plus a quick toggle in the side menu. |
| Message history | Keep deleted messages and inspect previous message edits received by this client. |
| Message labels | Customize the labels shown on deleted and edited messages. |
| SeeTg | Browse gifts with filters and sorting, view gift and profile history, and resolve unknown gift senders through SeeTg. |
| Languages | English, Russian, Ukrainian and Uzbek for SeeGram settings. |

Message history is local: it cannot recover messages or edits the client never received.
SeeTg features use the SeeTg service; availability depends on the data and access it provides.

## Download

Get available **Windows, macOS and Linux** packages from
[GitHub Releases](https://github.com/seegram/seegram-desktop/releases).
Release notes list the files and architectures included in each version.
SeeGram uses its own update feed for automatic updates.

## Build

Follow Telegram Desktop's platform instructions for the toolchain and dependencies:
[Windows](docs/building-win.md) · [macOS](docs/building-mac.md) · [Linux](docs/building-linux.md).

Supply your own Telegram API credentials, as required by the upstream build:

```sh
Telegram/configure.sh -D TDESKTOP_API_ID=YOUR_API_ID -D TDESKTOP_API_HASH=YOUR_API_HASH
cmake --build out --config Release --target Telegram
```

On Windows, use `Telegram\configure.bat x64` with the same CMake options.
See [fork/README.md](fork/README.md) for private local configuration, release secrets
and keeping the fork in sync with upstream.

## Credits & license

Built on [Telegram Desktop](https://github.com/telegramdesktop/tdesktop).
Ghost mode was inspired by [AyuGram](https://github.com/AyuGram/AyuGramDesktop).

[GPLv3 with the OpenSSL exception](LICENSE). See [LEGAL](LEGAL) for notices.
