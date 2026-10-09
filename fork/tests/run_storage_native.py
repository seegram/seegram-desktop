#!/usr/bin/env python3
"""Run the opt-in native storage scenario without opening any real profile."""
import argparse
from pathlib import Path
import plistlib
import shutil
import subprocess
import tempfile


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('app', type=Path, help='App built with SEEGRAM_STORAGE_REGRESSION=ON')
    args = parser.parse_args()
    source = args.app.resolve()
    if not (source / 'Contents/MacOS/SeeGram').is_file():
        raise SystemExit('SeeGram test binary is missing')
    stage = Path(tempfile.mkdtemp(prefix='seegram-730-storage-', dir='/private/tmp'))
    succeeded = False
    try:
        app = stage / 'SeeGram Storage Test.app'
        subprocess.run(['ditto', str(source), str(app)], check=True)
        info = app / 'Contents/Info.plist'
        with info.open('rb') as file:
            metadata = plistlib.load(file)
        metadata.update(CFBundleIdentifier='tg.see.SeeGram.tests.storage730',
                        CFBundleName='SeeGram Storage Test',
                        CFBundleDisplayName='SeeGram Storage Test')
        metadata.pop('CFBundleURLTypes', None)
        with info.open('wb') as file:
            plistlib.dump(metadata, file)
        subprocess.run(['codesign', '--force', '--deep', '--sign', '-', str(app)], check=True)
        profile = stage / 'profile'
        profile.mkdir()
        (profile / '.native-storage-test').touch()
        result = subprocess.run([str(app / 'Contents/MacOS/SeeGram'), '-many',
                                 '-workdir', str(profile), '-noupdate'],
                                cwd=profile, text=True, capture_output=True, timeout=180)
        (stage / 'stdout.txt').write_text(result.stdout)
        (stage / 'stderr.txt').write_text(result.stderr)
        if result.returncode or 'native storage checks' not in result.stdout:
            print(result.stdout[-2000:])
            print(result.stderr[-2000:])
            raise SystemExit(f'Native storage regression failed; diagnostics: {stage}')
        print(result.stdout.strip())
        succeeded = True
    finally:
        if succeeded:
            shutil.rmtree(stage)


if __name__ == '__main__':
    main()
