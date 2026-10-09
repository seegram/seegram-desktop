import importlib.util
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch

SPEC = importlib.util.spec_from_file_location(
    'windows_sdk_cache', Path(__file__).resolve().parents[1] / 'windows-sdk-cache.py')
MODULE = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(MODULE)


class WindowsSdkCacheTests(unittest.TestCase):
    def setUp(self):
        temporary = tempfile.TemporaryDirectory()
        self.addCleanup(temporary.cleanup)
        root = Path(temporary.name)
        self.build = root / 'out'
        self.build.mkdir()
        self.sdk = root / 'sdk'
        self.winrt = self.sdk / 'cppwinrt/winrt'
        (self.winrt / 'impl').mkdir(parents=True)
        (self.winrt / 'base.h').write_text('#define CPPWINRT_VERSION "2.0.250303.5"')
        (self.winrt / 'Windows.Security.Credentials.h').write_text('RequestCreateForWindowAsync')
        (self.winrt / 'impl/Windows.Security.Credentials.0.h').write_text('IKeyCredentialWithWindow')

    def test_sdk_update_invalidates_old_objects_and_preserves_current_and_unrelated_cache(self):
        old = self.build / 'old.obj'
        old.write_bytes(b'COFF\x00 /FAILIFMISMATCH:"C++/WinRT version=2.0.250303.1"\x00')
        current = self.build / 'current.obj'
        current.write_bytes(b'/FAILIFMISMATCH:"C++/WinRT version=2.0.250303.5"')
        unrelated = self.build / 'unrelated.obj'
        unrelated.write_bytes(b'/FAILIFMISMATCH:"RuntimeLibrary=MT_StaticRelease"')
        library = self.build / 'cached.lib'
        library.write_bytes(old.read_bytes())
        with patch('builtins.print'):
            self.assertEqual(MODULE.refresh(self.build, self.sdk), ['old.obj'])
            self.assertEqual(MODULE.refresh(self.build, self.sdk), [])
        self.assertFalse(old.exists())
        self.assertTrue(current.exists())
        self.assertTrue(unrelated.exists())
        self.assertTrue(library.exists())

    def test_unsupported_sdk_fails_without_touching_build_objects(self):
        obj = self.build / 'old.obj'
        obj.write_bytes(b'/FAILIFMISMATCH:"C++/WinRT version=2.0.250303.1"')
        (self.winrt / 'Windows.Security.Credentials.h').write_text('RequestCreateAsync')
        with self.assertRaisesRegex(SystemExit, 'Windows Hello declarations'):
            MODULE.refresh(self.build, self.sdk)
        self.assertTrue(obj.exists())
