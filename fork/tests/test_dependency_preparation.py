import importlib.util
from pathlib import Path
import subprocess
import tempfile
import unittest
from unittest.mock import patch

CHECKOUT = Path(__file__).resolve().parents[2]
SPEC = importlib.util.spec_from_file_location(
    'prepare_dependencies', CHECKOUT / 'fork/prepare-dependencies.py')
MODULE = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(MODULE)


class DependencyPreparationTests(unittest.TestCase):
    def setUp(self):
        self.directory = tempfile.TemporaryDirectory()
        self.addCleanup(self.directory.cleanup)
        self.root = Path(self.directory.name).resolve() / 'checkout'
        self.root.mkdir()
        self.script = self.root / 'fork/prepare-dependencies.py'
        for relative in ('fork/prepare-dependencies.py', 'Telegram/build/prepare/prepare.py'):
            path = self.root / relative
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_text('initial recipe')
        # The Qt choice is upstream's, so the real selection is exercised.
        (self.root / 'Telegram/build/qt_version.py').write_text(
            (CHECKOUT / 'Telegram/build/qt_version.py').read_text())
        self.addCleanup(patch.stopall)
        patch.object(MODULE, 'ROOT', self.root).start()
        patch.object(MODULE, '__file__', str(self.script)).start()
        patch('builtins.print').start()

    def qt_config(self, windows=False):
        libraries = self.root.parent / 'Libraries'
        prefix = (libraries / 'win64') if windows else (libraries / 'local')
        return prefix / ('Qt-' + MODULE.qt_version()) / 'lib/cmake/Qt6/Qt6Config.cmake'

    def install_qt(self, windows=False):
        path = self.qt_config(windows)
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text('prepared Qt')

    def artifacts(self, windows=False, qt=True):
        libraries = self.root.parent / 'Libraries'
        if qt:
            self.install_qt(windows)
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
        for tool in ('msys64', 'python', 'NuGet', 'jom', 'gyp', 'rust'):
            self.assertLess(stages.index(tool), stages.index('openssl3'))
        self.assertFalse(any(stage.startswith('qt') for stage in stages))
        self.assertTrue((self.root.parent / 'Libraries/win64/.seegram-dependencies-730').is_file())

    def test_missing_windows_qt_is_prepared_once_from_upstream_stages(self):
        patch.object(MODULE.platform, 'system', return_value='Windows').start()
        self.artifacts(windows=True, qt=False)
        qt = 'qt_' + MODULE.qt_version()
        with patch.dict(MODULE.os.environ, {'Platform': 'x64'}), \
                patch.object(MODULE.subprocess, 'run',
                             side_effect=lambda *args, **kwargs: self.install_qt(True)) as run:
            MODULE.native(False, False)
        stages = run.call_args.args[0][3:]
        for stage in ('qt6_windows7', 'tg_angle', qt):
            self.assertIn(stage, stages)
        (self.root / 'Telegram/build/prepare/prepare.py').write_text('new pinned libraries')
        with patch.dict(MODULE.os.environ, {'Platform': 'x64'}), \
                patch.object(MODULE.subprocess, 'run') as run:
            MODULE.native(False, False)
        self.assertNotIn(qt, run.call_args.args[0][3:])

    def test_missing_macos_qt_is_prepared_without_windows_patches(self):
        patch.object(MODULE.platform, 'system', return_value='Darwin').start()
        self.artifacts(qt=False)
        with patch.object(MODULE.subprocess, 'run',
                          side_effect=lambda *args, **kwargs: self.install_qt()) as run:
            MODULE.native(False, False)
        stages = run.call_args.args[0][3:]
        self.assertIn('qt_' + MODULE.qt_version(), stages)
        self.assertNotIn('qt6_windows7', stages)

    def test_unprepared_qt_fails_without_writing_success_stamp(self):
        patch.object(MODULE.platform, 'system', return_value='Darwin').start()
        libraries = self.artifacts(qt=False)
        with patch.object(MODULE.subprocess, 'run'):
            with self.assertRaises(SystemExit):
                MODULE.native(False, False)
        self.assertFalse((libraries / '.seegram-dependencies-730').exists())
        with self.assertRaises(SystemExit):
            MODULE.native(False, True)

    def test_qt_version_leaves_environment_unchanged(self):
        with patch.dict(MODULE.os.environ, {'QT': 'configured elsewhere'}):
            self.assertTrue(MODULE.qt_version().startswith('6.'))
            self.assertEqual(MODULE.os.environ['QT'], 'configured elsewhere')

    def test_powershell_developer_environment_supplies_upstream_platform(self):
        patch.object(MODULE.platform, 'system', return_value='Windows').start()
        self.artifacts(windows=True)
        with patch.dict(MODULE.os.environ, {'VSCMD_ARG_TGT_ARCH': 'x64'}, clear=True), \
                patch.object(MODULE.subprocess, 'run') as run:
            MODULE.native(False, False)
        self.assertEqual(run.call_args.kwargs['env']['Platform'], 'x64')
        self.assertEqual(run.call_args.kwargs['env']['VSLANG'], '1033')

    def test_actual_developer_architecture_overrides_stale_platform(self):
        patch.object(MODULE.platform, 'system', return_value='Windows').start()
        self.artifacts(windows=True)
        with patch.dict(MODULE.os.environ, {'VSCMD_ARG_TGT_ARCH': 'x86', 'Platform': 'x64'}), \
                patch.object(MODULE.subprocess, 'run') as run:
            with self.assertRaises(SystemExit):
                MODULE.native(False, False)
        run.assert_not_called()

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
