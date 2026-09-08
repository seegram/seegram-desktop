import contextlib
import io
from pathlib import Path
import subprocess
import unittest
from unittest.mock import patch


def sync_code():
    workflow = Path(__file__).resolve().parents[2] / '.github/workflows/seegram-release.yml'
    source = workflow.read_text()
    marker = '          python3 - "${{ github.sha }}" <<\'PY\'\n'
    block = source.split(marker, 1)[1].split('          PY\n', 1)[0]
    return '\n'.join(line[10:] for line in block.splitlines())


def result(code=0, error=''):
    return subprocess.CompletedProcess([], code, stdout='', stderr=error)


class ReleaseGitRetryTests(unittest.TestCase):
    def run_sync(self, responses):
        with patch('subprocess.run', side_effect=responses) as run, \
                patch('time.sleep') as sleep, \
                patch('sys.argv', ['-', 'release-sha']), \
                contextlib.redirect_stdout(io.StringIO()), \
                contextlib.redirect_stderr(io.StringIO()):
            code = 0
            try:
                exec(compile(sync_code(), '<release-sync>', 'exec'), {})
            except SystemExit as error:
                code = error.code
            return code, run.call_args_list, sleep.call_count

    def test_transient_lock_retries_without_removing_it(self):
        code, calls, sleeps = self.run_sync([
            result(128, 'Unable to create .git/index.lock: File exists.'),
            result(), result(),
        ])
        self.assertEqual((code, len(calls), sleeps), (0, 3, 1))
        self.assertEqual(calls[0].args[0], calls[1].args[0])
        self.assertEqual(calls[0].args[0],
                         ['git', 'reset', '--quiet', '--hard', 'release-sha'])
        self.assertEqual(calls[2].args[0][:3], ['git', 'submodule', 'update'])

    def test_unrelated_failure_is_not_retried(self):
        code, calls, sleeps = self.run_sync([
            result(128, 'fatal: unknown revision'),
        ])
        self.assertEqual((code, len(calls), sleeps), (128, 1, 0))

    def test_persistent_lock_has_a_bounded_wait(self):
        code, calls, sleeps = self.run_sync([
            result(128, 'Unable to create .git/index.lock: File exists.')
        ] * 60)
        self.assertEqual((code, len(calls), sleeps), (128, 60, 59))

    def test_submodule_index_lock_is_also_retried(self):
        code, calls, sleeps = self.run_sync([
            result(),
            result(128, 'Unable to create submodule/index.lock: File exists.'),
            result(),
        ])
        self.assertEqual((code, len(calls), sleeps), (0, 3, 1))
        self.assertEqual(calls[1].args[0], calls[2].args[0])


if __name__ == '__main__':
    unittest.main()
