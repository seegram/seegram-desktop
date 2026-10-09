import base64
import contextlib
import io
import json
import os
from pathlib import Path
import unittest
from unittest.mock import patch
from urllib.error import HTTPError

import yaml


class ReleasePreflightTests(unittest.TestCase):
    def setUp(self):
        workflow = Path(__file__).resolve().parents[2] / '.github/workflows/seegram-release.yml'
        job = yaml.load(workflow.read_text(), Loader=yaml.BaseLoader)['jobs']['validate']
        script = job['steps'][0]['run'].split("<<'PY'\n", 1)[1].rsplit('\nPY', 1)[0]
        self.code = compile(script, str(workflow), 'exec')
        self.tag = {'object': {'type': 'commit', 'sha': 'release-sha'}}
        self.counter = '1'
        self.calls = []

    def request(self, request, **kwargs):
        self.calls.append(request)
        url = request.full_url
        if '/contents/' in url:
            text = ('#define SEEGRAM_BUILD_COUNTER ' + self.counter + '\n'
                    if url.endswith('build_counter.h?ref=release-sha')
                    else 'constexpr auto AppVersionStr = "7.3";')
            result = {'content': base64.b64encode(text.encode()).decode()}
        elif '/git/ref/tags/' in url:
            if self.tag is None:
                raise HTTPError(url, 404, 'Not found', {}, None)
            result = self.tag
        elif request.get_method() == 'POST':
            self.assertEqual(json.loads(request.data),
                             {'ref': 'refs/tags/v7.3-1', 'sha': 'release-sha'})
            result = {'object': {'type': 'commit', 'sha': 'release-sha'}}
        else:
            self.fail('Unexpected GitHub request')
        return io.BytesIO(json.dumps(result).encode())

    def run_check(self, publish='true'):
        environment = dict(GH_TOKEN='test', RELEASE_REPO='seegram/seegram-desktop',
                           RELEASE_SHA='release-sha', RELEASE_COUNTER='1', RELEASE_PUBLISH=publish)
        with patch.dict(os.environ, environment), patch('urllib.request.urlopen', self.request), \
                contextlib.redirect_stdout(io.StringIO()):
            exec(self.code, {})

    def test_reserved_correct_tag_requires_no_write(self):
        self.run_check()
        self.assertTrue(all(call.get_method() == 'GET' for call in self.calls))

    def test_manual_publish_reserves_missing_tag(self):
        self.tag = None
        self.run_check()
        self.assertEqual(self.calls[-1].get_method(), 'POST')

    def test_different_tag_commit_is_never_replaced(self):
        self.tag['object']['sha'] = 'another-commit'
        with self.assertRaisesRegex(SystemExit, 'another commit'):
            self.run_check()
        self.assertTrue(all(call.get_method() == 'GET' for call in self.calls))

    def test_build_without_publishing_does_not_reserve_version(self):
        with self.assertRaises(SystemExit) as result:
            self.run_check('false')
        self.assertEqual(result.exception.code, 0)
        self.assertEqual(len(self.calls), 1)

    def test_counter_mismatch_stops_before_tag_operations(self):
        self.counter = '2'
        with self.assertRaisesRegex(SystemExit, 'committed build 2'):
            self.run_check()
        self.assertEqual(len(self.calls), 1)


if __name__ == '__main__':
    unittest.main()
