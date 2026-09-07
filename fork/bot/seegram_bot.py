#!/usr/bin/env python3
"""Owner-only release previews, pinned builds and restart-safe monitoring."""
import asyncio
import json
import logging
import os
import re
from pathlib import Path
import time

import aiohttp
from aiogram import Bot, Dispatcher, F
from aiogram.filters import Command, CommandObject
from aiogram.types import BotCommand, BotCommandScopeChat, InlineKeyboardButton, InlineKeyboardMarkup

from release_core import PLATFORMS, ReleaseError, ReleaseService

BOT_TOKEN = os.environ['BOT_TOKEN']
GH_TOKEN = os.environ['GH_TOKEN']
OWNER_ID = int(os.environ['OWNER_ID'])
REPO = os.environ['REPO']
WORKFLOW = os.environ.get('WORKFLOW', 'seegram-release.yml')
FEED_URL = os.environ.get('FEED_URL', 'https://desktop.see.tg/current4')
STATE_FILE = Path(os.environ.get('STATE_FILE', 'release-state.json'))
HEADERS = {'Authorization': 'Bearer ' + GH_TOKEN, 'Accept': 'application/vnd.github+json', 'X-GitHub-Api-Version': '2022-11-28'}
logging.basicConfig(level=logging.INFO, format='%(asctime)s %(levelname)s %(message)s')
dp = Dispatcher()
private_owner = (F.from_user.id == OWNER_ID) & (F.chat.type == 'private')
owner_callback = (F.from_user.id == OWNER_ID) & (F.message.chat.type == 'private')
lock = asyncio.Lock()
plans = {}
session = None
service = None
state = json.loads(STATE_FILE.read_text()) if STATE_FILE.exists() else None


def save():
    temporary = STATE_FILE.with_suffix('.new')
    temporary.write_text(json.dumps(state, ensure_ascii=False, indent=2) + '\n')
    temporary.chmod(0o600)
    os.replace(temporary, STATE_FILE)


def keyboard(rows):
    return InlineKeyboardMarkup(inline_keyboard=[[
        InlineKeyboardButton(text=text, callback_data=data) for text, data in row] for row in rows])


MENU = keyboard([[('Новый релиз', 'release'), ('Статус', 'status')], [('Проверка обновлений', 'feed'), ('Повторить сбой', 'retry')]])


async def github(method, path, allow_missing=False, **kwargs):
    async with session.request(method, 'https://api.github.com' + path, headers=HEADERS, **kwargs) as response:
        if response.status == 404 and allow_missing:
            return None
        if response.status >= 500:
            raise aiohttp.ServerConnectionError('GitHub temporarily unavailable')
        if response.status >= 400:
            try:
                body = await response.json(content_type=None)
                reason = body.get('message', 'ошибка API')[:200]
            except ValueError:
                reason = 'неожиданный ответ сервера'
            raise ReleaseError(f'GitHub {response.status}: {reason}')
        body = await response.text()
        return json.loads(body) if body else {}


async def fetch_feed():
    async with session.get(FEED_URL) as response:
        response.raise_for_status()
        feed = await response.json(content_type=None)
    for platform in PLATFORMS:
        value = feed.get(platform, {}).get('stable', {}).get('released')
        if not isinstance(value, str) or not value.isdecimal() or int(value) <= 0:
            raise ReleaseError(f'Лента содержит некорректную версию для {platform}.')
    return feed


def version_label(value):
    base, counter = int(value) >> 32, int(value) & 0xFFFFFFFF
    return f'{base // 1000000}.{base // 1000 % 1000}.{base % 1000} · build {counter}'


async def feed_report(expected=None):
    feed = await fetch_feed()
    lines = ['Обновления для клиентов:']
    for platform, label in PLATFORMS.items():
        entry = feed[platform]['stable']
        link = entry['link'].replace('{version}', entry['released'])
        if not link.startswith('/packages/') or '..' in link:
            raise ReleaseError('Некорректная ссылка пакета в ленте.')
        url = FEED_URL.rsplit('/', 1)[0] + link
        async with session.head(url) as response:
            healthy = response.status == 200 and int(response.headers.get('Content-Length', 0)) > 1000
        matches = expected is None or int(entry['released']) == expected
        lines.append(f'{"✅" if healthy and matches else "❌"} {label}: {version_label(entry["released"])}'
                     + ('' if healthy else ' — пакет недоступен')
                     + ('' if matches else ' — эта платформа ещё не обновлена'))
    return '\n'.join(lines)


async def preview(message, argument=''):
    if argument and (not argument.isdecimal() or len(argument) > 10):
        raise ReleaseError('Используйте /release для автоматического номера или /release 12.')
    async with lock:
        if state and not state.get('notified'):
            raise ReleaseError('Предыдущий запуск ещё отслеживается. Смотрите /status.')
        plan = await service.plan(int(argument) if argument else None)
        plans.clear()
        plans[plan.token] = plan
    await message.answer(
        f'Релиз SeeGram {plan.version} · build {plan.counter}\n'
        f'Исходники: {plan.head[:12]}\n'
        'Платформы: Windows x64, macOS Intel/Apple Silicon, Linux x64.\n\n'
        + (f'Счётчик {plan.current} → {plan.counter} будет записан отдельным коммитом.\n' if plan.counter != plan.current else 'Номер уже записан в исходниках.\n')
        + 'После подтверждения начнутся сборка, подпись и публикация обновлений.',
        reply_markup=keyboard([[('Собрать и опубликовать', 'confirm:' + plan.token)], [('Отмена', 'cancel:' + plan.token)]]))


async def status_report():
    runs = await service.runs()
    if not runs:
        return 'Релизов пока нет.'
    run = runs[0]
    jobs = await service.api('GET', f'/actions/runs/{run["id"]}/jobs?per_page=100')
    lines = [f'{run["display_title"]}\n{run["html_url"]}']
    for job in jobs['jobs']:
        result = job['conclusion'] or job['status']
        icon = '✅' if result == 'success' else '❌' if result in ('failure', 'cancelled', 'timed_out') else '⏳'
        step = next((s['name'] for s in job.get('steps', []) if s['conclusion'] == 'failure' or s['status'] == 'in_progress'), '')
        lines.append(f'{icon} {job["name"]}: {result}' + (f' — {step}' if step else ''))
    return '\n'.join(lines)


@dp.message(Command('start', 'help'), private_owner)
async def help_command(message):
    await message.answer('Релизы SeeGram\n\n/release — подготовить следующий релиз\n/release 12 — выбрать номер вручную\n/status — этапы сборки и ошибки\n/feed — версии и доступность пакетов\n/retry — повторить только упавшие платформы\n\nСначала покажу версию и коммит, затем попрошу подтвердить публикацию.', reply_markup=MENU)


async def execute(message, action, argument=''):
    try:
        if action == 'release':
            await preview(message, argument)
        elif action == 'status':
            await message.answer(await status_report(), reply_markup=MENU)
        elif action == 'retry':
            await retry(message)
        elif action == 'feed':
            await message.answer(await feed_report(), reply_markup=MENU)
    except (ReleaseError, aiohttp.ClientError, asyncio.TimeoutError, ValueError) as error:
        await message.answer(f'Не выполнено: {error if isinstance(error, ReleaseError) else "сервис временно недоступен; попробуйте ещё раз"}', reply_markup=MENU)


@dp.message(Command('release', 'status', 'feed', 'retry'), private_owner)
async def command_handler(message, command: CommandObject):
    await execute(message, command.command, (command.args or '').strip())


@dp.callback_query(owner_callback)
async def callback_handler(query):
    global state
    await query.answer()
    action, _, token = (query.data or '').partition(':')
    if action in ('release', 'status', 'feed', 'retry'):
        await execute(query.message, action)
        return
    if action == 'cancel':
        plans.pop(token, None)
        await query.message.edit_text('Подготовка релиза отменена.', reply_markup=MENU)
        return
    if action != 'confirm':
        return
    async with lock:
        plan = plans.pop(token, None)
        if not plan:
            await query.message.answer('Это подтверждение уже использовано или устарело.', reply_markup=MENU)
            return
        if state and not state.get('notified'):
            await query.message.answer('Предыдущий запуск ещё отслеживается.', reply_markup=MENU)
            return
        try:
            state = await service.prepare(plan)
            state.update(chat_id=query.message.chat.id, message_id=query.message.message_id)
            save()
            await query.message.edit_text(f'Подготовлен SeeGram {plan.version} build {plan.counter}. Запускаю Actions…')
            state['phase'] = 'dispatching'
            save()
            await service.dispatch(state)
            state['phase'] = 'dispatched'
            save()
        except (ReleaseError, aiohttp.ClientError, asyncio.TimeoutError, ValueError) as error:
            if state and state.get('phase') == 'dispatching' and isinstance(error, ReleaseError):
                state.update(notified=True, phase='rejected')
                save()
                await query.message.answer(f'GitHub отклонил запуск: {error}', reply_markup=MENU)
            elif state and state.get('phase') == 'dispatching':
                await query.message.answer('Ответ GitHub не получен. Проверяю, создался ли запуск; повторно автоматически не запускаю.')
            else:
                state = None
                save()
                await query.message.answer(f'Релиз не запущен: {error if isinstance(error, ReleaseError) else "ошибка сети"}', reply_markup=MENU)


async def retry(message):
    async with lock:
        if not state or not state.get('run_id') or not state.get('notified'):
            raise ReleaseError('Нет завершённого запуска нового бота для повтора. Используйте /release или /status.')
        await service.ensure_idle()
        run = await service.api('GET', f'/actions/runs/{state["run_id"]}')
        if run['conclusion'] not in ('failure', 'timed_out', 'cancelled') or run['head_sha'] != state['sha']:
            raise ReleaseError('Этот запуск нельзя повторить как неудачный релиз.')
        expected = (state['base'] << 32) | state['counter']
        feed = await fetch_feed()
        if any(int(feed[p]['stable']['released']) > expected for p in PLATFORMS):
            raise ReleaseError('В ленте уже есть более новый релиз. Повтор старого запрещён.')
        state.update(notified=False, phase='retrying', attempt=run['run_attempt'] + 1, created=time.time())
        save()
        try:
            await service.api('POST', f'/actions/runs/{state["run_id"]}/rerun-failed-jobs')
        except ReleaseError:
            state.update(notified=True, phase='rejected')
            save()
            raise
        except (aiohttp.ClientError, asyncio.TimeoutError):
            await message.answer('Ответ на повтор не получен. Проверяю номер попытки; дублировать запрос не буду.')
            return
        await message.answer('Повторяю только неудачные задания того же коммита.', reply_markup=MENU)


async def failure_details(jobs):
    lines = []
    for job in jobs:
        if job['conclusion'] != 'failure':
            continue
        try:
            url = 'https://api.github.com/repos/' + REPO + f'/actions/jobs/{job["id"]}/logs'
            async with session.get(url, headers=HEADERS, allow_redirects=False) as response:
                if response.status == 302:
                    location = response.headers['Location']
                    if not location.startswith('https://'):
                        continue
                elif response.status == 200:
                    text = (await response.content.read(2 * 1024 * 1024)).decode('utf-8', 'replace')
                    location = None
                else:
                    continue
            if location:
                async with session.get(location) as response:
                    response.raise_for_status()
                    text = (await response.content.read(2 * 1024 * 1024)).decode('utf-8', 'replace')
            errors = [re.sub(r'\x1b\[[0-9;]*m', '', line).strip() for line in text.splitlines()
                      if '[ERROR]' in line or '##[error]' in line]
            if errors:
                detail = errors[0].replace(GH_TOKEN, '***').replace(BOT_TOKEN, '***')[:350]
                lines.append(job['name'] + ': ' + detail)
        except (aiohttp.ClientError, asyncio.TimeoutError):
            continue
    return '\n'.join(lines)


async def monitor(bot):
    global state
    while True:
        try:
            if state and not state.get('notified'):
                if not state.get('run_id'):
                    run = await service.locate(state)
                    if run:
                        state['run_id'] = run['id']
                        state['phase'] = 'running'
                        save()
                    elif time.time() - state['created'] > 300:
                        await bot.send_message(state['chat_id'], 'Не удалось найти запуск за 5 минут. Проверьте /status перед повторной попыткой.', reply_markup=MENU)
                        state['notified'] = True
                        save()
                if state.get('run_id'):
                    run = await service.api('GET', f'/actions/runs/{state["run_id"]}')
                    if run.get('run_attempt', 1) < state.get('attempt', 1):
                        if time.time() - state['created'] > 300:
                            await bot.send_message(state['chat_id'], 'GitHub не подтвердил новую попытку. Проверьте /status.', reply_markup=MENU)
                            state['notified'] = True
                            save()
                        await asyncio.sleep(20)
                        continue
                    jobs = await service.api('GET', f'/actions/runs/{state["run_id"]}/jobs?per_page=100')
                    lines = [f'SeeGram {state["version"]} · build {state["counter"]}', run['html_url']]
                    for job in jobs['jobs']:
                        result = job['conclusion'] or job['status']
                        step = next((s['name'] for s in job.get('steps', []) if s['conclusion'] == 'failure' or s['status'] == 'in_progress'), '')
                        icon = '✅' if result == 'success' else '❌' if result in ('failure', 'cancelled', 'timed_out') else '⏳'
                        lines.append(f'{icon} {job["name"]}: {result}' + (f'\n   {step}' if step else ''))
                    text = '\n'.join(lines)
                    if state.get('last_text') != text:
                        try:
                            await bot.edit_message_text(text, chat_id=state['chat_id'], message_id=state['message_id'])
                        except Exception:
                            replacement = await bot.send_message(state['chat_id'], text)
                            state['message_id'] = replacement.message_id
                        state['last_text'] = text
                        save()
                    if run['status'] == 'completed':
                        expected = (state['base'] << 32) | state['counter']
                        report = await feed_report(expected)
                        verdict = 'Релиз опубликован.' if run['conclusion'] == 'success' and '❌' not in report else 'Релиз завершился не полностью. Смотрите статусы платформ и ленту ниже.'
                        details = await failure_details(jobs['jobs']) if run['conclusion'] != 'success' else ''
                        await bot.send_message(state['chat_id'], (verdict + '\n' + details + '\n\n' + report)[:4000], reply_markup=MENU)
                        state['notified'] = True
                        save()
        except asyncio.CancelledError:
            raise
        except Exception as error:
            logging.warning('Release monitoring will retry: %s', type(error).__name__)
        await asyncio.sleep(20)


async def main():
    global session, service
    async with aiohttp.ClientSession(timeout=aiohttp.ClientTimeout(total=30), headers={'User-Agent': 'SeeGram Release Bot/2.0'}) as session:
        service = ReleaseService(github, fetch_feed, REPO, WORKFLOW)
        async with Bot(BOT_TOKEN) as bot:
            await bot.set_my_commands([BotCommand(command=name, description=description) for name, description in [('release', 'Подготовить релиз'), ('status', 'Статус платформ'), ('feed', 'Проверить обновления'), ('retry', 'Повторить неудачные задания'), ('help', 'Помощь')]], scope=BotCommandScopeChat(chat_id=OWNER_ID))
            task = asyncio.create_task(monitor(bot))
            try:
                await dp.start_polling(bot)
            finally:
                task.cancel()
                await asyncio.gather(task, return_exceptions=True)


if __name__ == '__main__':
    asyncio.run(main())
