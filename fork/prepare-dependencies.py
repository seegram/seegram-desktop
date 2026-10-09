#!/usr/bin/env python3
"""Prepare the dependencies changed by the Telegram 7.3 update once per recipe.

Qt stays in its existing prepared directory. Linux uses a separate, content-
addressed upstream dependency image instead of replacing a shared image tag.
"""
import argparse
import hashlib
import os
from pathlib import Path
import platform
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[1]
STAGES = ["patches", "xz", "openssl3", "ffmpeg", "libheif", "breakpad",
          "tg_owt", "ada", "tlottie", "wallet-engine", "tdesktop_rust"]
WINDOWS_TOOLS = ["msys64", "python", "NuGet", "jom", "gyp", "rust"]


def fingerprint(paths):
    digest = hashlib.sha256()
    for path in sorted(paths):
        digest.update(path.relative_to(ROOT).as_posix().encode())
        digest.update(path.read_bytes())
    return digest.hexdigest()


def linux_image(check):
    context = ROOT / "Telegram/build/docker/centos_env"
    paths = [path for path in context.rglob("*") if path.is_file()
             and "__pycache__" not in path.parts]
    identity = fingerprint(paths)
    image = "seegram-dependencies:linux-" + identity[:20]
    found = subprocess.run(["docker", "image", "inspect", image],
                           stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    if found.returncode:
        if check:
            raise SystemExit("Linux dependencies need preparation: " + image)
        env = os.environ.copy()
        env.update(DEBUG="", LTO="", JOBS=os.environ.get("SEEGRAM_LINUX_JOBS", "4"))
        generated = subprocess.check_output(
            [sys.executable, str(context / "gen_dockerfile.py")], env=env)
        with tempfile.TemporaryDirectory(prefix="seegram-docker-") as temporary:
            dockerfile = Path(temporary) / "Dockerfile"
            dockerfile.write_bytes(generated)
            subprocess.run(["docker", "build", "--file", str(dockerfile),
                            "--tag", image, str(context)],
                           stdout=sys.stderr, check=True)
    print(image)


def native(check, record):
    system = platform.system()
    if system not in ("Darwin", "Windows"):
        raise SystemExit("Use --linux-image on Linux")
    stages = (WINDOWS_TOOLS + [stage for stage in STAGES if stage != "xz"] + ["tg_angle"]
              if system == "Windows" else STAGES)
    libraries = ROOT.parent / "Libraries"
    environment = os.environ.copy()
    if system == "Windows":
        architecture = environment.get("VSCMD_ARG_TGT_ARCH") or environment.get("Platform", "")
        if architecture.lower() not in ("x64", "amd64"):
            raise SystemExit("Use the x64 Visual Studio developer environment")
        environment["Platform"] = "x64"
        libraries /= "win64"
        artifacts = [libraries / "tdesktop_rust/out/lib/tdesktop_rust.lib",
                     libraries / "tdesktop_rust/out/src/wallet_engine/wallet_engine.cpp"]
    else:
        artifacts = [libraries / "local/lib/libtdesktop_rust.a",
                     libraries / "local/src/wallet_engine/wallet_engine.cpp"]
    recipe = ROOT / "Telegram/build/prepare/prepare.py"
    identity = fingerprint([Path(__file__).resolve(), recipe,
                            ROOT / "Telegram/build/qt_version.py"])
    stamp = libraries / ".seegram-dependencies-730"
    ready = all(path.is_file() for path in artifacts)
    if ready and stamp.is_file() and stamp.read_text().strip() == identity:
        print("Telegram 7.3 dependencies are ready.")
        return
    if check:
        raise SystemExit("Telegram 7.3 dependencies need preparation")
    if not record:
        subprocess.run([sys.executable, str(recipe), "silent", *stages],
                       cwd=ROOT, env=environment, check=True)
    if not all(path.is_file() for path in artifacts):
        raise SystemExit("Prepared Rust archive or generated wallet binding is missing")
    stamp.write_text(identity + "\n")
    print("Telegram 7.3 dependencies are ready.")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--check", action="store_true", help="Check without building")
    parser.add_argument("--record", action="store_true",
                        help="Record a completed manual preparation of the same stages")
    parser.add_argument("--linux-image", action="store_true",
                        help="Prepare and print the matching Linux image tag")
    args = parser.parse_args()
    if args.linux_image:
        linux_image(args.check)
    else:
        native(args.check, args.record)
