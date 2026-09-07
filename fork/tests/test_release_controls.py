import asyncio
import base64
import hashlib
import json
import multiprocessing
from pathlib import Path
import struct
import sys
import tempfile
import time
import unittest

FORK = Path(__file__).resolve().parents[1]
sys.path[:0] = [str(FORK), str(FORK / 'bot')]
from release_core import COUNTER_PATH, Plan, ReleaseError, ReleaseService, choose_counter
from publish_feed import TARGETS, publish, verify_package
from cryptography.hazmat.primitives import serialization
from cryptography.hazmat.primitives.asymmetric.ed25519 import Ed25519PrivateKey


def feed(version):
    return {p: {'stable': {'released': str(version), 'link': '/packages/seegram-{version}-' + p + '.tdup'}} for p in TARGETS}


def signed_package(platform, version, payload=b'compressed payload'):
    root, key = Ed25519PrivateKey.generate(), Ed25519PrivateKey.generate()
    raw = key.public_key().public_bytes(serialization.Encoding.Raw, serialization.PublicFormat.Raw)
    manifest = json.dumps({'format': 1, 'manifest_version': 1, 'expires': int(time.time()) + 3600,
        'keys': [{'id': 'test', 'alg': 'Ed25519', 'x': base64.urlsafe_b64encode(raw).decode().rstrip('=')}],
        'channels': {'stable': [['test']]}, 'revoked': []}).encode()
    blob = lambda b: struct.pack('<I', len(b)) + b
    system, arch = TARGETS[platform]
    region = b'TDUP' + struct.pack('<IBBBQQ', 2, 0, system, arch, version, int(time.time())) + blob(manifest)
    package = region + blob(root.sign(manifest)) + struct.pack('<I', 1) + blob(b'test')
    package += blob(key.sign(region + hashlib.sha256(payload).digest())) + blob(payload)
    public = root.public_key().public_bytes(serialization.Encoding.PEM, serialization.PublicFormat.SubjectPublicKeyInfo)
    return package, public


def publish_worker(root, platform, version, public):
    publish(root, platform, version, Path(root) / (platform + '.upload'), public)


class CounterTests(unittest.TestCase):
    def test_new_base_and_published_counter(self):
        self.assertEqual(choose_counter(7002005, 1, feed((7001004 << 32) | 1)), 1)
        self.assertEqual(choose_counter(7002005, 1, feed((7002005 << 32) | 1)), 2)
        self.assertEqual(choose_counter(7002005, 8, feed((7002005 << 32) | 1)), 8)
        self.assertEqual(choose_counter(7002005, 1, {}, 228), 228)

    def test_rollback_and_out_of_range(self):
        for args in [(7002005, 1, {}, 0), (7002005, 1, {}, 2**32),
                     (7002005, 1, feed((7002006 << 32) | 1), None),
                     (7002005, 1, feed((7002005 << 32) | 10), 10)]:
            with self.assertRaises(ReleaseError):
                choose_counter(*args)


class ServiceTests(unittest.IsolatedAsyncioTestCase):
    async def asyncSetUp(self):
        self.calls = []
        self.head = 'old-head'
        self.runs = []
        self.source = '#define SEEGRAM_BUILD_COUNTER 1\n'
        async def request(method, path, **kwargs):
            self.calls.append((method, path, kwargs))
            if '/actions/workflows/' in path and method == 'GET': return {'workflow_runs': self.runs}
            if path.endswith('/git/ref/heads/main'): return {'object': {'sha': self.head}}
            if '/git/commits/' in path: return {'tree': {'sha': 'old-tree'}}
            if path.endswith('/git/blobs'): return {'sha': 'blob'}
            if path.endswith('/git/trees'): return {'sha': 'tree'}
            if path.endswith('/git/commits'): return {'sha': 'new-head'}
            return {}
        async def current_feed(): return feed((7001004 << 32) | 1)
        self.service = ReleaseService(request, current_feed, 'seegram/seegram-desktop', 'seegram-release.yml')
        self.plan = Plan(self.head, 7002005, '7.2.5', 2, 1, self.source, 'unique-token', time.time())

    async def test_prepare_commits_then_pins_and_dispatches(self):
        state = await self.service.prepare(self.plan)
        blob = next(c[2]['json'] for c in self.calls if c[1].endswith('/git/blobs'))
        self.assertEqual(blob['content'], '#define SEEGRAM_BUILD_COUNTER 2\n')
        update = next(c[2]['json'] for c in self.calls if c[0] == 'PATCH')
        self.assertIs(update['force'], False)
        self.assertEqual(state['sha'], 'new-head')
        await self.service.dispatch(state)
        dispatch = self.calls[-1][2]['json']
        self.assertEqual(dispatch['ref'], 'release-build/unique-token')
        self.assertEqual(dispatch['inputs']['counter'], '2')

    async def test_changed_main_never_mutates(self):
        self.head = 'someone-elses-commit'
        with self.assertRaises(ReleaseError): await self.service.prepare(self.plan)
        self.assertTrue(all(c[0] == 'GET' for c in self.calls))

    async def test_active_run_blocks_prepare(self):
        self.runs = [{'status': 'queued'}]
        with self.assertRaises(ReleaseError): await self.service.prepare(self.plan)
        self.assertTrue(all(c[0] == 'GET' for c in self.calls))

    async def test_run_tracking_uses_token_and_commit(self):
        self.runs = [{'id': 1, 'display_title': 'other-token', 'head_sha': 'new-head'},
                     {'id': 2, 'display_title': 'unique-token', 'head_sha': 'wrong-head'},
                     {'id': 3, 'display_title': 'SeeGram unique-token', 'head_sha': 'new-head'}]
        run = await self.service.locate({'token': 'unique-token', 'sha': 'new-head'})
        self.assertEqual(run['id'], 3)


class FeedTests(unittest.TestCase):
    def setUp(self):
        self.directory = tempfile.TemporaryDirectory()
        self.addCleanup(self.directory.cleanup)
        self.root = Path(self.directory.name)
        (self.root / 'packages').mkdir()
        self.version = (7002005 << 32) | 1
        old = feed((7001004 << 32) | 1)
        old['win'] = old['win64']
        old['winarm'] = old['win64']
        (self.root / 'current4').write_text(json.dumps(old))

    def test_signature_architecture_and_tamper(self):
        package, public = signed_package('win64', self.version)
        source = self.root / 'win64.upload'; source.write_bytes(package)
        self.assertEqual(verify_package(source, 'win64', self.version, public)['signature'], 'valid')
        with self.assertRaises(ValueError): verify_package(source, 'armac', self.version, public)
        with self.assertRaises(ValueError): verify_package(source, 'win64', self.version + 1, public)
        source.write_bytes(package[:-1] + bytes([package[-1] ^ 1]))
        with self.assertRaises(ValueError): verify_package(source, 'win64', self.version, public)

    def test_concurrent_platforms_do_not_overwrite_each_other(self):
        processes = []
        context = multiprocessing.get_context('spawn')
        for platform in TARGETS:
            package, public = signed_package(platform, self.version)
            (self.root / (platform + '.upload')).write_bytes(package)
            process = context.Process(target=publish_worker, args=(str(self.root), platform, self.version, public))
            process.start(); processes.append(process)
        for process in processes:
            process.join(15)
            self.assertEqual(process.exitcode, 0)
        result = json.loads((self.root / 'current4').read_text())
        self.assertEqual(set(result), set(TARGETS))
        self.assertTrue(all(int(v['stable']['released']) == self.version for v in result.values()))
        self.assertEqual((self.root / 'current').read_bytes(), (self.root / 'current4').read_bytes())

    def test_immutable_versions_and_rollback(self):
        source = self.root / 'win64.upload'
        package, public = signed_package('win64', self.version)
        source.write_bytes(package)
        publish(self.root, 'win64', self.version, source, public)
        old_feed = (self.root / 'current4').read_bytes()
        replacement, replacement_public = signed_package('win64', self.version, b'changed payload')
        source.write_bytes(replacement)
        with self.assertRaises(ValueError): publish(self.root, 'win64', self.version, source, replacement_public)
        self.assertEqual((self.root / 'current4').read_bytes(), old_feed)
        older, older_public = signed_package('win64', self.version - 1)
        source.write_bytes(older)
        with self.assertRaises(ValueError): publish(self.root, 'win64', self.version - 1, source, older_public)
        self.assertEqual((self.root / 'current4').read_bytes(), old_feed)


if __name__ == '__main__':
    unittest.main()
