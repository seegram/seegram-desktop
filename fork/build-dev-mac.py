#!/usr/bin/env python3
"""Build an isolated macOS developer app without touching release outputs."""
import os
from pathlib import Path
import platform
import plistlib
import shutil
import subprocess
import tempfile


def main():
    if platform.system() != "Darwin":
        raise SystemExit("This script requires macOS")
    root = Path(__file__).resolve().parents[1]
    build = root / "out/dev-build"
    destination = root / "out/dev/SeeGram Dev.app"
    profile = Path.home() / "Library/Application Support/SeeGramDev"
    build.mkdir(parents=True, exist_ok=True)
    cache = {}
    for path in (root / "out/CMakeCache.txt", build / "CMakeCache.txt"):
        if path.exists():
            for line in path.read_text().splitlines():
                if "=" in line and ":" in line and not line.startswith(("//", "#")):
                    key, value = line.split("=", 1)
                    cache[key.split(":", 1)[0]] = value
    env = os.environ.copy()
    if cache.get("qt_requested"):
        env["QT"] = cache["qt_requested"]
    # Keep API credentials out of process arguments and terminal output.
    with tempfile.NamedTemporaryFile(mode="w", prefix="dev-config-", suffix=".cmake", dir=build) as config:
        for key in ("TDESKTOP_API_ID", "TDESKTOP_API_HASH"):
            value = env.get(key) or cache.get(key)
            if not value:
                raise SystemExit(f"Set {key} or configure the existing out/ tree first")
            value = value.replace("\\", "\\\\").replace('"', '\\"').replace("$", "\\$")
            config.write(f'set({key} "{value}" CACHE STRING "" FORCE)\n')
        config.flush()
        subprocess.run([
            "cmake", "-S", str(root), "-B", str(build), "-G", "Xcode",
            "-C", config.name, "-U", "Qt6*_DIR", "-U", "QT_DIR",
            "-DDESKTOP_APP_DISABLE_AUTOUPDATE=ON",
            "-DDESKTOP_APP_SPECIAL_TARGET=mac",
            f"-DDESKTOP_APP_MAC_ARCH={platform.machine()}",
            "-DCMAKE_BUILD_TYPE=Debug",
        ], env=env, check=True)
    subprocess.run([
        "cmake", "--build", str(build), "--config", "Debug",
        "--target", "Telegram", "-j", "8",
    ], check=True)
    source = build / "Debug/SeeGram.app"
    if not (source / "Contents/MacOS/SeeGram").is_file():
        raise SystemExit("Developer app was not produced")
    destination.parent.mkdir(parents=True, exist_ok=True)
    profile.mkdir(parents=True, exist_ok=True)
    # The native portable-mode check also covers restarts without CLI arguments.
    portable = destination.parent / "TelegramForcePortable"
    if portable.is_symlink():
        if portable.resolve() != profile.resolve():
            raise SystemExit("Existing dev portable link points at another profile")
    elif portable.exists():
        raise SystemExit("Existing dev portable folder must be preserved; refusing to replace it")
    else:
        portable.symlink_to(profile, target_is_directory=True)
    stage = Path(tempfile.mkdtemp(prefix="app-stage-", dir=destination.parent))
    try:
        app = stage / destination.name
        subprocess.run(["ditto", str(source), str(app)], check=True)
        info = app / "Contents/Info.plist"
        with info.open("rb") as f:
            metadata = plistlib.load(f)
        metadata.update(CFBundleIdentifier="tg.see.SeeGram.dev", CFBundleName="SeeGram Dev", CFBundleDisplayName="SeeGram Dev")
        # Keep dev copies from claiming Telegram links used by the main app.
        metadata.pop("CFBundleURLTypes", None)
        with info.open("wb") as f:
            plistlib.dump(metadata, f)
        subprocess.run(["codesign", "--force", "--deep", "--sign", "-", str(app)], check=True)
        subprocess.run(["codesign", "--verify", "--deep", "--strict", str(app)], check=True)
        if destination.exists():
            shutil.rmtree(destination)
        app.rename(destination)
    finally:
        shutil.rmtree(stage)
    print(f"Developer app: {destination}")
    print(f"Developer data: {profile}")
    print("Auto-updates: disabled at build time")


if __name__ == "__main__":
    main()
