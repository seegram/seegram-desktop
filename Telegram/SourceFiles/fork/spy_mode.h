/*
This file is part of SeeGram Desktop,
a Telegram Desktop fork.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include <rpl/producer.h>

// Spy mode: keep what the other side takes back.
//
// A message deleted on the server normally vanishes from the client too, and
// an edit overwrites the text with no trace of what it replaced. With these
// switches on the client keeps a deleted message in the chat, marked as
// deleted, and remembers every earlier text of an edited one.
//
// The idea and the shape of the switches come from AyuGram ("Save Deleted
// Messages", "Save Edits History", "Save in Bot Dialogs"). The implementation
// is written against tdesktop's own deletion and edition paths, so that the
// hooks stay one line each - see fork/deleted_messages.h and
// fork/edit_history.h for the logic, fork/message_store.h for the storage.
//
// Settings live in their own file, never in Core::Settings - that one has a
// hand-written binary serializer, so every upstream field addition would
// collide. See fork/RULES.md.

namespace Fork::Spy {

struct Settings {
	bool saveDeletedMessages = true;
	bool saveEditsHistory = true;
	bool saveForBots = false;

	friend inline bool operator==(
		const Settings &,
		const Settings &) = default;
};

// Read from disk once at startup and then kept in memory: consulted on
// every deletion update and every edit the server sends.
[[nodiscard]] const Settings &Current();

// Applies and persists in one step.
void Set(const Settings &settings);

// Called once from Core::Application before any session exists.
void Start();

[[nodiscard]] rpl::producer<Settings> Changes();
[[nodiscard]] rpl::producer<Settings> Value();

} // namespace Fork::Spy
