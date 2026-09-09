#!/usr/bin/env python3
"""Build a revision-specific tlottie fix in the CMake output directory.

Never edit the prepared libraries or their sources. A new upstream pin selects
the ordinary upstream library, which must pass gradient-regression.cpp before
CMake can finish configuring the client.
"""
import argparse
import hashlib
import os
from pathlib import Path
import platform
import re
import shutil
import subprocess
import tarfile
import tempfile
import urllib.request


AFFECTED = "4b940c7942fbde8ee56f10f39a5224a4153bd91e"
TOOLCHAIN = "1.96.1"
HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[1]


def run(command, **kwargs):
    return subprocess.run([str(x) for x in command], check=True, **kwargs)


def upstream_pin():
    recipe = (ROOT / "Telegram/build/prepare/prepare.py").read_text()
    match = re.search(r"stage\('tlottie',.*?git checkout ([0-9a-f]{10,40})", recipe, re.S)
    if not match:
        raise RuntimeError("Cannot identify upstream tlottie pin; review fork/tlottie")
    return match[1]


def rust_environment(output):
    host_arch = {"arm64": "aarch64", "AMD64": "x86_64"}.get(platform.machine(), platform.machine())
    host_os = {"Darwin": "apple-darwin", "Windows": "pc-windows-msvc", "Linux": "unknown-linux-gnu"}[platform.system()]
    host = f"{host_arch}-{host_os}"
    rust = ROOT.parent / "ThirdParty/rust"
    suffix = ".exe" if os.name == "nt" else ""
    relative = Path(f"rustup/toolchains/{TOOLCHAIN}-{host}/bin")
    if not (rust / relative / f"cargo{suffix}").is_file():
        # Upstream's Linux dependency image deletes Rust after building its
        # archive. Keep our minimal toolchain in the build cache, not $HOME.
        if platform.system() != "Linux":
            raise RuntimeError("Prepared Rust toolchain missing; run Telegram/build/prepare first")
        rust = output / "rust"
        if not (rust / relative / "cargo").is_file():
            rust.mkdir(parents=True, exist_ok=True)
            installer = rust / "rustup-init"
            url = f"https://static.rust-lang.org/rustup/dist/{host}/rustup-init"
            urllib.request.urlretrieve(url, installer)
            with urllib.request.urlopen(url + ".sha256", timeout=60) as response:
                expected = response.read().decode().split()[0]
            if hashlib.sha256(installer.read_bytes()).hexdigest() != expected:
                raise RuntimeError("Rust installer checksum mismatch")
            installer.chmod(0o755)
            env = os.environ.copy()
            env.update(RUSTUP_HOME=str(rust / "rustup"), CARGO_HOME=str(rust / "cargo"))
            run([installer, "-y", "--no-modify-path", "--profile", "minimal",
                 "--default-host", host, "--default-toolchain", TOOLCHAIN], env=env)
    env = os.environ.copy()
    env.update(RUSTUP_HOME=str(rust / "rustup"), CARGO_HOME=str(rust / "cargo"),
               RUSTUP_TOOLCHAIN=TOOLCHAIN, RUSTC=str(rust / relative / f"rustc{suffix}"))
    # Do not inherit target/ABI overrides from an unrelated Cargo build.
    for key in ("RUSTFLAGS", "CARGO_ENCODED_RUSTFLAGS", "CARGO_BUILD_TARGET", "RUSTC_WRAPPER"):
        env.pop(key, None)
    return rust / relative / f"cargo{suffix}", env


def source_archive(libraries, output, destination):
    source = libraries / "tlottie"
    has_commit = (source / ".git").exists() and subprocess.run(
        ["git", "-C", str(source), "cat-file", "-e", f"{AFFECTED}^{{commit}}"],
        stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL,
    ).returncode == 0
    if not has_commit:
        source = output / "upstream"
        source.mkdir(exist_ok=True)
        if not (source / ".git").exists():
            run(["git", "init", source])
        run(["git", "-C", source, "fetch", "--depth=1",
             "https://github.com/dkaraush/tlottie.git", AFFECTED])
    archive = destination.parent / "source.tar"
    run(["git", "-C", source, "archive", "--output", archive, AFFECTED])
    with tarfile.open(archive) as contents:
        # This is our fixed upstream commit, but also reject escaping paths.
        for member in contents.getmembers():
            path = Path(member.name)
            if path.is_absolute() or ".." in path.parts or member.issym() or member.islnk():
                raise RuntimeError("Unexpected path in tlottie source archive")
        contents.extractall(destination)
    archive.unlink()
    env = os.environ.copy()
    env["GIT_CEILING_DIRECTORIES"] = str(destination.parent)
    run(["git", "apply", "--check", HERE / "gradient-work.patch"], cwd=destination, env=env)
    run(["git", "apply", HERE / "gradient-work.patch"], cwd=destination, env=env)


def prepare(options):
    output = options.output.resolve()
    output.mkdir(parents=True, exist_ok=True)
    selection = output / "selected.cmake"
    pin = upstream_pin()
    if not AFFECTED.startswith(pin):
        selection.write_text("# New upstream pin: use and test the official library.\n")
        print(f"tlottie {pin}: using upstream library; regression check required")
        return
    identity = hashlib.sha256()
    for path in (Path(__file__), HERE / "gradient-work.patch"):
        identity.update(path.read_bytes())
    identity.update((AFFECTED + TOOLCHAIN + ";".join(options.target)).encode())
    cache = output / identity.hexdigest()[:20]
    archive_name = "tlottie.lib" if os.name == "nt" else "libtlottie.a"
    library = cache / archive_name
    if not library.is_file():
        cargo, env = rust_environment(output)
        stage = Path(tempfile.mkdtemp(prefix="build-", dir=output))
        try:
            source = stage / "source"
            source.mkdir()
            source_archive(options.libraries, output, source)
            archives = []
            for target in options.target:
                command = [cargo, "rustc", "--lib", "--release", "--locked", "--features", "c-api",
                           "--crate-type", "staticlib", "--target", target, "--target-dir", stage / "target"]
                target_env = env.copy()
                if "win7-windows-msvc" in target:
                    command.extend(["-Z", "build-std=std,panic_abort"])
                    target_env["RUSTC_BOOTSTRAP"] = "1"
                if "windows-msvc" in target:
                    command.extend(["--config", f"target.{target}.rustflags=['-C','target-feature=+crt-static']"])
                run(command, cwd=source, env=target_env)
                archives.append(stage / "target" / target / "release" / archive_name)
            if len(archives) == 1:
                shutil.copy2(archives[0], stage / archive_name)
            else:
                run(["lipo", "-create", *archives, "-output", stage / archive_name])
            # Publish only complete archives. Preserve source for audit/tests.
            shutil.rmtree(stage / "target")
            if cache.exists():
                shutil.rmtree(cache)
            stage.rename(cache)
        finally:
            if stage.exists():
                shutil.rmtree(stage)
        print(f"tlottie {pin}: built temporary gradient accounting fix")
    selection.write_text(f'set(seegram_tlottie_library "{library.as_posix()}")\n')


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--libraries", type=Path, required=True)
    parser.add_argument("--target", action="append", required=True)
    prepare(parser.parse_args())
