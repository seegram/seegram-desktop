#!/usr/bin/env python3
"""Validate signed packages and publish one platform under a shared file lock."""
import argparse
import base64
import fcntl
import hashlib
import json
import os
from pathlib import Path
import struct
import tempfile
import time

TARGETS = {'win64': (0, 1), 'mac': (1, 1), 'armac': (1, 2), 'linux': (2, 1)}


def verify_package(path, platform, version, root_public):
    from cryptography.hazmat.primitives.serialization import load_pem_public_key
    from cryptography.hazmat.primitives.asymmetric.ed25519 import Ed25519PublicKey
    data = Path(path).read_bytes()
    offset = 0

    def take(size):
        nonlocal offset
        if size < 0 or offset + size > len(data):
            raise ValueError('Truncated update package')
        value = data[offset:offset + size]
        offset += size
        return value

    def number(fmt):
        return struct.unpack('<' + fmt, take(struct.calcsize('<' + fmt)))[0]

    def blob(maximum):
        size = number('I')
        if not 0 < size <= maximum:
            raise ValueError('Invalid update field length')
        return take(size)

    if take(4) != b'TDUP' or number('I') != 2:
        raise ValueError('Not a v2 update package')
    channel, system, arch = number('B'), number('B'), number('B')
    actual_version, created = number('Q'), number('Q')
    if channel != 0 or (system, arch) != TARGETS[platform] or actual_version != int(version):
        raise ValueError('Package channel, architecture or version does not match the feed')
    manifest_bytes = blob(1024 * 1024)
    signed_region = data[:offset]
    manifest_signature = blob(4096)
    load_pem_public_key(root_public).verify(manifest_signature, manifest_bytes)
    manifest = json.loads(manifest_bytes)
    now = int(time.time())
    if manifest.get('format') != 1 or manifest['expires'] <= now:
        raise ValueError('Unsupported or expired signing manifest')
    signatures = {}
    count = number('I')
    if count > 128:
        raise ValueError('Too many package signatures')
    for _ in range(count):
        key_id = blob(1024).decode()
        signatures[key_id] = blob(4096)
    payload = blob(1024 * 1024 * 1024)
    if offset != len(data):
        raise ValueError('Trailing package bytes')
    signing_input = signed_region + hashlib.sha256(payload).digest()
    valid = set()
    for key in manifest['keys']:
        key_id = key['id']
        if key_id in manifest.get('revoked', []) or key_id not in signatures:
            continue
        if key.get('expires', now + 1) <= now or key['alg'] != 'Ed25519':
            continue
        try:
            Ed25519PublicKey.from_public_bytes(base64.urlsafe_b64decode(key['x'] + '==')).verify(
                signatures[key_id], signing_input)
            valid.add(key_id)
        except Exception:
            continue
    groups = manifest['channels'].get('stable', [])
    if not groups or not all(valid.intersection(group) for group in groups):
        raise ValueError('Package signature is not authorized for stable updates')
    return {'platform': platform, 'version': str(version), 'bytes': len(data),
            'sha256': hashlib.sha256(data).hexdigest(), 'signature': 'valid'}


def replace_json(path, data):
    descriptor, temporary = tempfile.mkstemp(prefix='.current-', dir=path.parent)
    try:
        with os.fdopen(descriptor, 'w') as output:
            json.dump(data, output, indent=2)
            output.write('\n')
            output.flush()
            os.fsync(output.fileno())
            os.fchmod(output.fileno(), 0o644)
        os.replace(temporary, path)
    finally:
        if os.path.exists(temporary):
            os.unlink(temporary)


def publish(root, platform=None, version=None, source=None, root_public=None):
    root = Path(root)
    with (root / '.publish.lock').open('a') as lock:
        fcntl.flock(lock, fcntl.LOCK_EX)
        path = root / 'current4'
        feed = json.loads(path.read_text())
        if platform:
            if platform not in TARGETS:
                raise ValueError('Unsupported release platform')
            report = verify_package(source, platform, version, root_public)
            previous = int(feed.get(platform, {}).get('stable', {}).get('released', 0))
            if int(version) < previous:
                raise ValueError('Refusing to roll the update feed back')
            destination = root / 'packages' / f'seegram-{version}-{platform}.tdup'
            if destination.exists():
                if hashlib.sha256(destination.read_bytes()).hexdigest() != report['sha256']:
                    raise ValueError('This version already has a different immutable package')
                if Path(source) != destination:
                    Path(source).unlink()
            else:
                os.chmod(source, 0o644)
                os.replace(source, destination)
            feed.setdefault(platform, {})['stable'] = {
                'released': str(version),
                'link': '/packages/seegram-{version}-' + platform + '.tdup',
            }
        for unsupported in ('win', 'winarm'):
            feed.pop(unsupported, None)
        replace_json(path, feed)
        alias = root / '.current-link-new'
        alias.unlink(missing_ok=True)
        alias.symlink_to('current4')
        os.replace(alias, root / 'current')
        return feed


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--audit', action='store_true')
    parser.add_argument('--prune', action='store_true')
    args = parser.parse_args()
    root = Path(os.environ['SEEGRAM_ROOT'])
    public = base64.b64decode(os.environ.get('SEEGRAM_ROOT_PUBLIC', ''))
    if args.audit:
        feed = json.loads((root / 'current4').read_text())
        for platform in TARGETS:
            version = feed[platform]['stable']['released']
            print(json.dumps(verify_package(root / 'packages' / f'seegram-{version}-{platform}.tdup', platform, version, public)))
        return
    if args.prune:
        publish(root)
        print('Windows x86 and ARM64 removed; current follows current4')
        return
    platform, version = os.environ['SEEGRAM_PLATFORM'], os.environ['SEEGRAM_VERSION']
    source = root / 'packages' / f'seegram-{version}-{platform}.tdup.upload'
    publish(root, platform, version, source, public)
    print('Verified and published ' + platform + ' ' + version)


if __name__ == '__main__':
    main()
