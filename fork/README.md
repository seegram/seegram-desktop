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

## Releases

The manually triggered [SeeGram Release workflow](../.github/workflows/seegram-release.yml)
builds on the project's prepared macOS, Windows and Linux runners.
Set the repository's GitHub Actions secrets `TDESKTOP_API_ID` and
`TDESKTOP_API_HASH` before running it. The workflow requires both and forwards
them to the build, including the Linux container.

The platform entry points are `fork/release.sh`, `fork/release.ps1` and
`fork/release-linux.sh`. Each accepts a fork build counter and an option to
build without publishing; usage is documented at the top of each script.
Signing and deployment use the separate `SEEGRAM_*` settings named in the
workflow. Private signing keys remain on the runners.

## Upstream updates

The fork is maintained as commits on top of Telegram Desktop. Keep changes
focused on SeeGram features and their necessary integration points; preserve
upstream code, submodules and platform documentation where possible.

- `fork/list.sh` lists fork commits.
- `fork/update-upstream.sh [upstream-ref]` rebases them onto an upstream revision.
- [RULES.md](RULES.md) describes the maintenance conventions.
- [AGENTS.md](../AGENTS.md) contains repository instructions for coding agents.
