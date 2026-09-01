/*
This file is part of SeeGram Desktop,
a Telegram Desktop fork.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include <rpl/producer.h>

// Ghost mode: stop the client telling the server things it does not have to.
//
// Four separate switches rather than one, because they are four different
// trades. Not sending read receipts is invisible to the other side; not
// sending an online status changes what everyone sees about you; suppressing
// typing notifications sits between the two. Bundling them would force a
// choice nobody actually wants to make.
//
// The idea comes from AyuGram, which pioneered it in a Telegram Desktop fork.
// This implementation is written against tdesktop's own send paths rather
// than adapted from theirs: they weave the checks through upstream files,
// which suits a fork that does not rebase and would cost this one a conflict
// on every update.
//
// Settings live in their own file, never in Core::Settings - that one has a
// hand-written binary serializer, so every upstream field addition would
// collide.

namespace Fork::Ghost {

struct Settings {
	bool blockReadReceipts = false;
	bool blockTyping = false;
	bool blockOnlineStatus = false;
	bool blockUploadProgress = false;

	[[nodiscard]] bool anyEnabled() const {
		return blockReadReceipts
			|| blockTyping
			|| blockOnlineStatus
			|| blockUploadProgress;
	}

	// What the single switch in the side menu means: everything, or nothing.
	// Following AyuGram, where "ghost mode" is on only while every part of it
	// is - a half-on ghost mode would claim more than it does.
	[[nodiscard]] bool allEnabled() const {
		return blockReadReceipts
			&& blockTyping
			&& blockOnlineStatus
			&& blockUploadProgress;
	}

	friend inline bool operator==(
		const Settings &,
		const Settings &) = default;
};

// Read from disk once at startup and then kept in memory: these are consulted
// on hot paths, down to every keystroke that would send a typing notification.
[[nodiscard]] const Settings &Current();

// Applies and persists in one step - a setting that survives only until the
// next launch is worse than none.
void Set(const Settings &settings);

// Called once from Core::Application before any session exists.
void Start();

// The side menu switch and the settings section are two views of one value,
// and either can be open while the other changes it.
[[nodiscard]] rpl::producer<Settings> Changes();
[[nodiscard]] rpl::producer<Settings> Value();

// The master switch: all four at once. Reads back as on only while all four
// are, so flipping one part off in settings visibly turns ghost mode off.
[[nodiscard]] bool Enabled();
void SetEnabled(bool enabled);

// Convenience for the call sites, so that each hook stays one condition long.
[[nodiscard]] bool BlocksReadReceipts();
[[nodiscard]] bool BlocksTyping();
[[nodiscard]] bool BlocksOnlineStatus();
[[nodiscard]] bool BlocksUploadProgress();

} // namespace Fork::Ghost
