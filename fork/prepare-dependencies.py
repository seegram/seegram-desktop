#!/usr/bin/env python3
"""Prepare the dependencies upstream updates change once per recipe.

Qt takes longest by far, so it is rebuilt only when it is missing or the
upstream patches it is built with have changed; the stages it depends on are
prepared with it. Linux uses a separate, content-addressed upstream dependency
image instead of replacing a shared image tag.
"""
import argparse
import contextlib
import hashlib
import importlib.util
import io
import os
from pathlib import Path
import platform
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[1]
STAGES = ["patches", "xz", "openssl3", "libvpx", "libjxl", "ffmpeg", "libheif",
          "breakpad", "tg_owt", "ada", "tlottie", "wallet-engine", "tdesktop_rust"]
WINDOWS_TOOLS = ["msys64", "python", "NuGet", "jom", "gyp", "rust"]


def fingerprint(paths, root=None):
    digest = hashlib.sha256()
    for path in sorted(paths):
        digest.update(path.relative_to(root or ROOT).as_posix().encode())
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


def qt_version():
    """The Qt version upstream's own scripts choose for this platform."""
    spec = importlib.util.spec_from_file_location(
        "qt_version", ROOT / "Telegram/build/qt_version.py")
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    # resolve() reports its choice through the environment, and announces it
    # on Windows, which would end up in the --qt-version output.
    previous = os.environ.get("QT")
    try:
        architecture = "x64" if platform.system() == "Windows" else ""
        with contextlib.redirect_stdout(io.StringIO()):
            supported = module.resolve(architecture)
        if not supported:
            raise SystemExit("Upstream does not support this platform")
        return os.environ["QT"]
    finally:
        if previous is None:
            os.environ.pop("QT", None)
        else:
            os.environ["QT"] = previous


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
        environment["VSLANG"] = "1033"
        libraries /= "win64"
        artifacts = [libraries / "tdesktop_rust/out/lib/tdesktop_rust.lib",
                     libraries / "tdesktop_rust/out/src/wallet_engine/wallet_engine.cpp"]
    else:
        artifacts = [libraries / "local/lib/libtdesktop_rust.a",
                     libraries / "local/src/wallet_engine/wallet_engine.cpp"]
    qt = qt_version()
    major = qt.split(".")[0]
    windows_qt6 = system == "Windows" and int(major) >= 6
    qt_prefix = (libraries if system == "Windows" else libraries / "local") / ("Qt-" + qt)
    qt_config = qt_prefix / "lib/cmake" / ("Qt" + major) / ("Qt" + major + "Config.cmake")
    # The patch files upstream's Qt stage depends on; on Windows Qt 6 is also
    # built from the qt6_windows7 patches, and macOS applies the High Sierra ones.
    qt_patches = ["patches/qtbase_" + qt + "/*.patch", "patches/qtsvg_" + qt + "/*.patch"]
    if windows_qt6:
        qt_patches.append("qt6_windows7/*.patch")
    elif system == "Darwin":
        qt_patches.append("patches/qt6_highsierra/*.patch")
    qt_stamp = qt_prefix / ".seegram-patches"

    def qt_patched():
        return fingerprint([path for pattern in qt_patches for path in libraries.glob(pattern)],
                           libraries)

    recipe = ROOT / "Telegram/build/prepare/prepare.py"
    identity = fingerprint([Path(__file__).resolve(), recipe,
                            ROOT / "Telegram/build/qt_version.py"])
    stamp = libraries / ".seegram-dependencies-730"
    ready = qt_config.is_file() and all(path.is_file() for path in artifacts)
    if ready and stamp.is_file() and stamp.read_text().strip() == identity:
        print("Upstream dependencies are ready.")
        return
    if check:
        raise SystemExit("Upstream dependencies need preparation")
    if not record:
        # Whether Qt is stale depends on the patches, so they are updated first.
        sources = ["patches", *(["qt6_windows7"] if windows_qt6 else [])]
        subprocess.run([sys.executable, str(recipe), "silent", *sources],
                       cwd=ROOT, env=environment, check=True)
        # Named stages always rebuild and Qt takes longest by far, so an
        # installed Qt is kept until upstream changes its patches. A missing
        # one, such as Windows' after the move to Qt 6 in Telegram 7.3.2, is
        # built as well; on Windows upstream builds it with tg_angle.
        built = qt_stamp.read_text().strip() if qt_stamp.is_file() else ""
        qt_ready = qt_config.is_file() and built == qt_patched()
        subprocess.run([sys.executable, str(recipe), "silent",
                        *[stage for stage in stages if stage not in sources],
                        *([] if qt_ready else ["qt_" + qt])],
                       cwd=ROOT, env=environment, check=True)
    if not qt_config.is_file():
        raise SystemExit("Prepared Qt " + qt + " is missing")
    if not all(path.is_file() for path in artifacts):
        raise SystemExit("Prepared Rust archive or generated wallet binding is missing")
    qt_stamp.write_text(qt_patched() + "\n")
    stamp.write_text(identity + "\n")
    print("Upstream dependencies are ready.")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--check", action="store_true", help="Check without building")
    parser.add_argument("--record", action="store_true",
                        help="Record a completed manual preparation of the same stages")
    parser.add_argument("--linux-image", action="store_true",
                        help="Prepare and print the matching Linux image tag")
    parser.add_argument("--qt-version", action="store_true",
                        help="Print the Qt version this platform builds with")
    args = parser.parse_args()
    if args.qt_version:
        print(qt_version())
    elif args.linux_image:
        linux_image(args.check)
    else:
        native(args.check, args.record)
