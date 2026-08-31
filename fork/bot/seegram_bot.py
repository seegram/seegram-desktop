"""Release control for SeeGram Desktop over Telegram.

Deliberately thin. The bot starts the release workflow through the GitHub API
and reports what came back; it never builds, signs or uploads anything itself.
That is not a shortcut - the signing key lives on the build machines and has
to stay there, and self-hosted runners already solve job dispatch, retries and
log streaming. A bot doing those things itself would be a worse copy of
machinery that already exists.

Configuration comes from the environment, never from this file:

    BOT_TOKEN   the Telegram bot token
    GH_TOKEN    a GitHub token with actions:write on the repository
    OWNER_ID    the only Telegram user id allowed to use the bot
    REPO        owner/name of the repository

See fork/bot/README.md for the systemd unit.
"""

import asyncio
import logging
import os

import aiohttp
from aiogram import Bot, Dispatcher, F
from aiogram.filters import Command, CommandObject
from aiogram.types import Message

BOT_TOKEN = os.environ["BOT_TOKEN"]
GH_TOKEN = os.environ["GH_TOKEN"]
OWNER_ID = int(os.environ["OWNER_ID"])
REPO = os.environ["REPO"]
WORKFLOW = os.environ.get("WORKFLOW", "seegram-release.yml")
FEED_URL = os.environ.get("FEED_URL", "https://desktop.see.tg/current4")

API = "https://api.github.com"
HEADERS = {
    "Authorization": f"Bearer {GH_TOKEN}",
    "Accept": "application/vnd.github+json",
}

logging.basicConfig(level=logging.INFO, format="%(asctime)s %(message)s")
dispatcher = Dispatcher()

# Every handler is behind this. A bot that can publish an update onto everyone's
# machine must not take orders from whoever happens to find it.
owner_only = F.from_user.id == OWNER_ID


async def github(session, method, path, **kwargs):
    async with session.request(method, API + path, headers=HEADERS, **kwargs) as r:
        body = await r.text()
        if r.status >= 400:
            raise RuntimeError(f"GitHub {r.status}: {body[:200]}")
        return await r.json() if body else {}


async def feed_summary(session) -> str:
    async with session.get(FEED_URL) as r:
        feed = await r.json(content_type=None)
    lines = []
    for platform, channels in sorted(feed.items()):
        released = channels.get("stable", {}).get("released", "?")
        # The wire format is (upstream version << 32 | fork build).
        try:
            value = int(released)
        except (TypeError, ValueError):
            lines.append(f"{platform}: {released}")
            continue
        base, build = value >> 32, value & 0xFFFFFFFF
        major, rest = divmod(base, 1000000)
        minor, patch = divmod(rest, 1000)
        lines.append(f"{platform}: {major}.{minor}.{patch} build {build}")
    return "Serving now:\n" + "\n".join(lines)


@dispatcher.message(Command("start", "help"), owner_only)
async def help_command(message: Message):
    await message.answer(
        "SeeGram release control\n\n"
        "/release <n> - build and publish fork build n on every runner\n"
        "/status - the last few workflow runs\n"
        "/feed - what the update server is serving right now"
    )


@dispatcher.message(Command("release"), owner_only)
async def release_command(message: Message, command: CommandObject):
    counter = (command.args or "").strip()
    if not counter.isdigit() or int(counter) < 1:
        await message.answer("Usage: /release 6")
        return

    async with aiohttp.ClientSession() as session:
        try:
            await github(
                session,
                "POST",
                f"/repos/{REPO}/actions/workflows/{WORKFLOW}/dispatches",
                json={"ref": "main", "inputs": {"counter": counter, "publish": True}},
            )
        except RuntimeError as error:
            await message.answer(f"Could not start the release:\n{error}")
            return

        await message.answer(f"Build {counter} started. Watching it.")

        # A dispatch returns no run id, so find the run it just created.
        run = None
        for _ in range(12):
            await asyncio.sleep(5)
            runs = await github(session, "GET", f"/repos/{REPO}/actions/runs?per_page=5")
            for candidate in runs.get("workflow_runs", []):
                if candidate["path"].endswith(WORKFLOW) and candidate["status"] != "completed":
                    run = candidate
                    break
            if run:
                break
        if not run:
            await message.answer("Started, but I lost track of the run - see /status.")
            return

        while run["status"] != "completed":
            await asyncio.sleep(20)
            run = await github(session, "GET", f"/repos/{REPO}/actions/runs/{run['id']}")

        verdict = "finished" if run["conclusion"] == "success" else run["conclusion"]
        await message.answer(f"Build {counter} {verdict}.\n{run['html_url']}")
        if run["conclusion"] == "success":
            await message.answer(await feed_summary(session))


@dispatcher.message(Command("feed"), owner_only)
async def feed_command(message: Message):
    async with aiohttp.ClientSession() as session:
        await message.answer(await feed_summary(session))


@dispatcher.message(Command("status"), owner_only)
async def status_command(message: Message):
    async with aiohttp.ClientSession() as session:
        runs = await github(session, "GET", f"/repos/{REPO}/actions/runs?per_page=5")
    lines = [
        f"{run['conclusion'] or run['status']}: {run['display_title'][:50]}"
        for run in runs.get("workflow_runs", [])
    ]
    await message.answer("\n".join(lines) or "No runs yet.")


async def main():
    bot = Bot(BOT_TOKEN)
    logging.info("release control up, owner %s, repo %s", OWNER_ID, REPO)
    await dispatcher.start_polling(bot)


if __name__ == "__main__":
    asyncio.run(main())
