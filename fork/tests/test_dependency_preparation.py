import importlib.util
from pathlib import Path
import subprocess
import tempfile
import unittest
from unittest.mock import patch

SPEC = importlib.util.spec_from_file_location(
    'prepare_dependencies', Path(__file__).resolve().parents[1] / 'prepare-dependencies.py')
MODULE = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(MODULE)


class DependencyPreparationTests(unittest.TestCase):
    def setUp(self):
        self.directory = tempfile.TemporaryDirectory()
        self.addCleanup(self.directory.cleanup)
        self.root = Path(self.directory.name).resolve() / 'checkout'
        self.root.mkdir()
        self.script = self.root / 'fork/prepare-dependencies.py'
        for relative in ('fork/prepare-dependencies.py', 'Telegram/build/prepare/prepare.py',
                         'Telegram/build/qt_version.py'):
            path = self.root / relative
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_text('initial recipe')
        self.addCleanup(patch.stopall)
        patch.object(MODULE, 'ROOT', self.root).start()
        patch.object(MODULE, '__file__', str(self.script)).start()
        patch('builtins.print').start()

    def artifacts(self, windows=False):
        libraries = self.root.parent / 'Libraries'
        if windows:
            libraries /= 'win64'
            names = ('tdesktop_rust/out/lib/tdesktop_rust.lib',
                     'tdesktop_rust/out/src/wallet_engine/wallet_engine.cpp')
        else:
            names = ('local/lib/libtdesktop_rust.a',
                     'local/src/wallet_engine/wallet_engine.cpp')
        for name in names:
            path = libraries / name
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_bytes(b'prepared artifact')
        return libraries

    def test_warm_native_recipe_skips_build_and_missing_binding_invalidates_it(self):
        patch.object(MODULE.platform, 'system', return_value='Darwin').start()
        libraries = self.artifacts()
        MODULE.native(False, True)
        with patch.object(MODULE.subprocess, 'run') as run:
            MODULE.native(False, False)
            run.assert_not_called()
        (libraries / 'local/src/wallet_engine/wallet_engine.cpp').unlink()
        with self.assertRaises(SystemExit):
            MODULE.native(True, False)

    def test_changed_recipe_rebuilds_dependencies_without_rebuilding_qt(self):
        patch.object(MODULE.platform, 'system', return_value='Darwin').start()
        self.artifacts()
        MODULE.native(False, True)
        (self.root / 'Telegram/build/prepare/prepare.py').write_text('new pinned libraries')
        with patch.object(MODULE.subprocess, 'run') as run:
            MODULE.native(False, False)
        stages = run.call_args.args[0][3:]
        self.assertIn('tdesktop_rust', stages)
        self.assertIn('ffmpeg', stages)
        self.assertFalse(any(stage.startswith('qt') for stage in stages))

    def test_windows_uses_supported_stages_and_separate_library_cache(self):
        patch.object(MODULE.platform, 'system', return_value='Windows').start()
        self.artifacts(windows=True)
        with patch.dict(MODULE.os.environ, {'Platform': 'x64'}), \
                patch.object(MODULE.subprocess, 'run') as run:
            MODULE.native(False, False)
        stages = run.call_args.args[0][3:]
        self.assertNotIn('xz', stages)
        self.assertIn('tg_angle', stages)
        self.assertIn('wallet-engine', stages)
        self.assertTrue((self.root.parent / 'Libraries/win64/.seegram-dependencies-730').is_file())

    def test_failed_preparation_never_writes_success_stamp(self):
        patch.object(MODULE.platform, 'system', return_value='Darwin').start()
        libraries = self.artifacts()
        with patch.object(MODULE.subprocess, 'run', side_effect=subprocess.CalledProcessError(1, 'prepare')):
            with self.assertRaises(subprocess.CalledProcessError):
                MODULE.native(False, False)
        self.assertFalse((libraries / '.seegram-dependencies-730').exists())

    def test_linux_image_is_selected_by_recipe_without_replacing_shared_tag(self):
        context = self.root / 'Telegram/build/docker/centos_env'
        context.mkdir(parents=True)
        (context / 'Dockerfile').write_text('FROM base')
        (context / 'gen_dockerfile.py').write_text('render')
        with patch.object(MODULE.subprocess, 'run', return_value=subprocess.CompletedProcess([], 0)) as run:
            MODULE.linux_image(True)
            first = run.call_args.args[0][-1]
            (context / 'Dockerfile').write_text('FROM new-base')
            MODULE.linux_image(True)
            second = run.call_args.args[0][-1]
        self.assertNotEqual(first, second)
        self.assertTrue(first.startswith('seegram-dependencies:linux-'))
        self.assertNotIn('tdesktop:centos_env', (first, second))


if __name__ == '__main__':
    unittest.main()
