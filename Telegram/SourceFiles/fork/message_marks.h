/*
This file is part of SeeGram Desktop,
a Telegram Desktop fork.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include <rpl/producer.h>

// How a kept message announces itself: the mark drawn before the time of a
// deleted message, the word drawn before the time of an edited one, and
// whether deleted messages are painted translucent. Following AyuGram, the
// marks are free text so that a broom can become "deleted" or nothing at all.
//
// Settings live in their own file, never in Core::Settings. See fork/RULES.md.

namespace Fork::Marks {

struct Settings {
	QString deletedMark;
	QString editedMark;
	bool translucentDeleted = false;

	friend inline bool operator==(
		const Settings &,
		const Settings &) = default;
};

[[nodiscard]] const Settings &Current();
void Set(const Settings &settings);

// Called once from Core::Application before any session exists.
void Start();

[[nodiscard]] rpl::producer<Settings> Changes();
[[nodiscard]] rpl::producer<Settings> Value();

// The stored strings resolved for drawing: an empty deleted mark falls back
// to the default one, an empty edited mark to the client's own "edited",
// which cannot be baked into the default because the language may change.
[[nodiscard]] QString DeletedMark();
[[nodiscard]] QString EditedMark();
[[nodiscard]] QString DefaultDeletedMark();
[[nodiscard]] QString DefaultEditedMark();

[[nodiscard]] bool TranslucentDeleted();

} // namespace Fork::Marks
