"""Run with the release bot's aiogram/aiohttp environment; no network calls."""
import asyncio
import importlib
import json
import os
from pathlib import Path
import sys
import tempfile
import time
from types import SimpleNamespace
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'bot'))
from release_core import Plan


class BotTests(unittest.IsolatedAsyncioTestCase):
    async def asyncSetUp(self):
        self.directory = tempfile.TemporaryDirectory()
        self.addCleanup(self.directory.cleanup)
        os.environ.update(BOT_TOKEN='123456:local-test-token', GH_TOKEN='local-test-token', OWNER_ID='42',
                          REPO='seegram/seegram-desktop', STATE_FILE=str(Path(self.directory.name) / 'state.json'))
        import seegram_bot
        self.bot = importlib.reload(seegram_bot)
        self.sent = []
        async def answer(text, **kwargs):
            self.sent.append(text)
            return SimpleNamespace(message_id=2)
        self.message = SimpleNamespace(chat=SimpleNamespace(id=42, type='private'), message_id=1,
                                       answer=answer, edit_text=answer)
        self.query = SimpleNamespace(from_user=SimpleNamespace(id=42), message=self.message, data='', answer=lambda: asyncio.sleep(0))
        self.plan = Plan('head', 7002005, '7.2.5', 1, 1, '#define SEEGRAM_BUILD_COUNTER 1', 'token', time.time())

    async def test_access_filters_reject_other_users_and_groups(self):
        own = SimpleNamespace(from_user=SimpleNamespace(id=42), chat=SimpleNamespace(type='private'))
        other = SimpleNamespace(from_user=SimpleNamespace(id=43), chat=SimpleNamespace(type='private'))
        group = SimpleNamespace(from_user=SimpleNamespace(id=42), chat=SimpleNamespace(type='group'))
        self.assertTrue(self.bot.private_owner.resolve(own))
        self.assertFalse(self.bot.private_owner.resolve(other))
        self.assertFalse(self.bot.private_owner.resolve(group))
        self.assertTrue(self.bot.owner_callback.resolve(self.query))
        self.query.from_user.id = 43
        self.assertFalse(self.bot.owner_callback.resolve(self.query))

    async def test_uncertain_dispatch_is_persisted_not_replayed(self):
        calls = []
        async def prepare(plan):
            return {'phase': 'prepared', 'notified': False, 'run_id': None, 'token': plan.token,
                    'sha': plan.head, 'ref': 'release-build/token', 'created': time.time(),
                    'base': plan.base, 'version': plan.version, 'counter': plan.counter}
        async def dispatch(state):
            calls.append(state['token'])
            raise asyncio.TimeoutError()
        self.bot.service = SimpleNamespace(prepare=prepare, dispatch=dispatch)
        self.bot.plans['token'] = self.plan
        self.query.data = 'confirm:token'
        await self.bot.callback_handler(self.query)
        saved = json.loads(self.bot.STATE_FILE.read_text())
        self.assertEqual(saved['phase'], 'dispatching')
        self.assertEqual(saved['sha'], 'head')
        await self.bot.callback_handler(self.query)
        self.assertEqual(calls, ['token'])
        self.bot = importlib.reload(self.bot)
        self.assertEqual(self.bot.state['token'], 'token')
        self.assertFalse(self.bot.state['notified'])

    async def test_stale_button_never_prepares(self):
        self.query.data = 'confirm:expired'
        await self.bot.callback_handler(self.query)
        self.assertIn('устарело', self.sent[-1])
        self.assertIsNone(self.bot.state)


if __name__ == '__main__':
    unittest.main()
