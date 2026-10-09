#!/usr/bin/env python3
"""Invalidate only MSVC objects that retain an older C++/WinRT SDK version."""
import argparse
import mmap
from pathlib import Path
import re


def refresh(build, sdk):
    winrt = sdk / 'cppwinrt/winrt'
    credentials = winrt / 'Windows.Security.Credentials.h'
    interfaces = winrt / 'impl/Windows.Security.Credentials.0.h'
    if (not credentials.is_file() or not interfaces.is_file()
            or 'RequestCreateForWindowAsync' not in credentials.read_text()
            or 'IKeyCredentialWithWindow' not in interfaces.read_text()):
        raise SystemExit('Update Windows SDK 26100: Windows Hello declarations are missing')
    match = re.search(r'#define\s+CPPWINRT_VERSION\s+"([^"]+)"',
                      (winrt / 'base.h').read_text())
    if not match:
        raise SystemExit('Could not identify the SDK C++/WinRT version')
    current = match[1].encode('ascii')
    directive = re.compile(rb'/FAILIFMISMATCH:"C\+\+/WinRT version=([^"\x00\s]+)"')
    removed = []
    for obj in build.rglob('*.obj'):
        if obj.is_symlink() or not obj.stat().st_size:
            continue
        with obj.open('rb') as stream, mmap.mmap(stream.fileno(), 0, access=mmap.ACCESS_READ) as data:
            stale = any(version != current for version in directive.findall(data))
        if stale:
            obj.unlink()
            removed.append(obj.relative_to(build).as_posix())
    print(f'C++/WinRT {match[1]}: invalidated {len(removed)} stale object(s).')
    for name in removed:
        print('  ' + name)
    return removed


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--build-dir', type=Path, required=True)
    parser.add_argument('--sdk-root', type=Path, required=True)
    args = parser.parse_args()
    refresh(args.build_dir, args.sdk_root)
