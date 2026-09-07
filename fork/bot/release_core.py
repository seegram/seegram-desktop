"""GitHub release preparation, independent of Telegram handlers."""
import base64
from dataclasses import asdict, dataclass
import re
import time
import uuid

PLATFORMS = {'win64': 'Windows x64', 'armac': 'macOS Apple Silicon', 'mac': 'macOS Intel', 'linux': 'Linux x64'}
COUNTER_PATH = 'Telegram/SourceFiles/fork/build_counter.h'
VERSION_PATH = 'Telegram/SourceFiles/core/version.h'
COUNTER_RE = re.compile(r'(^#define SEEGRAM_BUILD_COUNTER )(\d+)$', re.M)


class ReleaseError(Exception):
    pass


def choose_counter(base, committed, feed, requested=None):
    if not 1 <= committed <= 0xFFFFFFFF:
        raise ReleaseError('Некорректный счётчик в исходниках.')
    published = [int(feed[p]['stable']['released']) for p in PLATFORMS if p in feed]
    if any(value >> 32 > base for value in published):
        raise ReleaseError('В ленте уже есть более новая версия Telegram. Откат запрещён.')
    highest = max((value & 0xFFFFFFFF for value in published if value >> 32 == base), default=0)
    next_counter = max(committed, highest + 1)
    result = next_counter if requested is None else requested
    if not next_counter <= result <= 0xFFFFFFFF:
        raise ReleaseError(f'Номер должен быть от {next_counter} до 4294967295.')
    return result


@dataclass
class Plan:
    head: str
    base: int
    version: str
    counter: int
    current: int
    content: str
    token: str
    created: float

    def serialize(self):
        return asdict(self)


class ReleaseService:
    def __init__(self, request, feed, repo, workflow):
        self.request, self.feed, self.repo, self.workflow = request, feed, repo, workflow
        self.prefix = '/repos/' + repo

    async def api(self, method, path, **kwargs):
        return await self.request(method, self.prefix + path, **kwargs)

    async def runs(self):
        result = await self.api('GET', f'/actions/workflows/{self.workflow}/runs?event=workflow_dispatch&per_page=30')
        return result['workflow_runs']

    async def ensure_idle(self):
        if any(run['status'] != 'completed' for run in await self.runs()):
            raise ReleaseError('Релиз уже выполняется или ожидает запуска. Смотрите /status.')

    async def head(self):
        return (await self.api('GET', '/git/ref/heads/main'))['object']['sha']

    async def file(self, path, sha):
        result = await self.api('GET', f'/contents/{path}?ref={sha}')
        return base64.b64decode(result['content']).decode()

    async def plan(self, requested=None):
        await self.ensure_idle()
        head = await self.head()
        content = await self.file(COUNTER_PATH, head)
        version_file = await self.file(VERSION_PATH, head)
        match = COUNTER_RE.search(content)
        base = re.search(r'AppVersion\s*=\s*(\d+)', version_file)
        version = re.search(r'AppVersionStr\s*=\s*"([^"]+)"', version_file)
        if not all((match, base, version)):
            raise ReleaseError('Не удалось прочитать версию исходников.')
        current, base = int(match[2]), int(base[1])
        counter = choose_counter(base, current, await self.feed(), requested)
        tag = f'v{version[1]}-{counter}'
        release = await self.api('GET', f'/releases/tags/{tag}', allow_missing=True)
        if release:
            raise ReleaseError(f'Релиз {tag} уже существует. Выберите следующий номер.')
        return Plan(head, base, version[1], counter, current, content, uuid.uuid4().hex, time.time())

    async def prepare(self, plan):
        if time.time() - plan.created > 600:
            raise ReleaseError('Подтверждение устарело. Нажмите «Новый релиз» ещё раз.')
        await self.ensure_idle()
        if await self.head() != plan.head:
            raise ReleaseError('main изменился после предпросмотра. Подготовьте релиз заново.')
        choose_counter(plan.base, plan.current, await self.feed(), plan.counter)
        head = plan.head
        if plan.counter != plan.current:
            commit = await self.api('GET', '/git/commits/' + head)
            content = COUNTER_RE.sub(lambda m: m[1] + str(plan.counter), plan.content, count=1)
            blob = await self.api('POST', '/git/blobs', json={'content': content, 'encoding': 'utf-8'})
            tree = await self.api('POST', '/git/trees', json={'base_tree': commit['tree']['sha'], 'tree': [
                {'path': COUNTER_PATH, 'mode': '100644', 'type': 'blob', 'sha': blob['sha']}]})
            commit = await self.api('POST', '/git/commits', json={'message': f'fork: prepare {plan.version} build {plan.counter}', 'tree': tree['sha'], 'parents': [head]})
            await self.api('PATCH', '/git/refs/heads/main', json={'sha': commit['sha'], 'force': False})
            head = commit['sha']
        ref = 'release-build/' + plan.token
        await self.api('POST', '/git/refs', json={'ref': 'refs/tags/' + ref, 'sha': head})
        return {'token': plan.token, 'sha': head, 'ref': ref, 'counter': plan.counter,
                'base': plan.base, 'version': plan.version, 'created': time.time(),
                'phase': 'prepared', 'run_id': None, 'notified': False}

    async def dispatch(self, state):
        await self.api('POST', f'/actions/workflows/{self.workflow}/dispatches', json={
            'ref': state['ref'], 'inputs': {'counter': str(state['counter']), 'publish': True, 'request_id': state['token']}})

    async def locate(self, state):
        for run in await self.runs():
            if state['token'] in run['display_title'] and run['head_sha'] == state['sha']:
                return run
        return None
