/*
This file is part of SeeGram Desktop,
a Telegram Desktop fork.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

// How the client signs in to see.tg: with the same Telegram Mini App
// initData the see.tg mini app itself gets when it opens inside Telegram.
//
// The client asks Telegram to open @seetgbot's main app the way it would
// for the user, without showing anything, and takes the launch data out of
// the URL Telegram answers with. The backend validates that data against
// the bot's token, so nothing but Telegram itself can mint it. It is kept
// for an hour per account, in memory and on disk, and thrown away when the
// backend stops accepting it.

namespace Main {
class Session;
} // namespace Main

namespace Fork::SeeTg::Auth {

// Calls done with the initData string, or fail with a short reason. Both
// may run synchronously when the data is already at hand.
void Request(
	not_null<Main::Session*> session,
	Fn<void(QString)> done,
	Fn<void(QString)> fail);

// Forget the cached data, so the next Request mints new one.
void Invalidate(not_null<Main::Session*> session);

} // namespace Fork::SeeTg::Auth
