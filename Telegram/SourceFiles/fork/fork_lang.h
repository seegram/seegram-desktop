/*
This file is part of SeeGram Desktop,
a Telegram Desktop fork.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include <rpl/producer.h>

// The fork's own strings, in the fork's own languages.
//
// Upstream's lang.strings is rewritten by every release and its keys are
// served from the cloud; a key added there is a conflict on every rebase and
// a hole in every language pack. The few dozen strings this fork adds live
// here instead, in a table with one column per language.
//
// Which column is used follows the language of the application, unless the
// user picks one on the fork's settings page. That override is a setting of
// its own, in its own file, never in Core::Settings. See fork/RULES.md.

namespace Fork::Lang {

enum class Language {
	SameAsApp,
	English,
	Russian,
	Ukrainian,
	Uzbek,

	Count,
};

enum class Key {
	GhostMode,
	DontSendReadReceipts,
	DontSendTyping,
	DontSendOnlineStatus,
	DontSendUploadProgress,
	GhostAbout,

	SpyMode,
	SpyEssentials,
	SaveDeletedMessages,
	SaveEditsHistory,
	SaveForBots,
	SpyAboutSaving,
	SpyAboutBots,

	Messages,
	DeletedMark,
	EditedMark,
	TranslucentDeleted,
	Reset,
	MarksAbout,

	SeeGramAbout,
	Categories,
	Links,
	SourceCode,
	NewsChannel,
	LanguageTitle,
	LanguageSameAsApp,

	EditHistory,
	DeletedMessages,
	ViewDeleted,
	ClearDeleted,
	Clear,
	Original,
	Edited,
	Current,
	DeletedAt,
	NoText,
	NothingSaved,
	ShowingLast,
	ClearConfirm,

	Count,
};

// The string in the language in use right now, and a producer of it that
// follows both the override and the application language.
[[nodiscard]] QString Text(Key key);
[[nodiscard]] rpl::producer<QString> Value(Key key);

// The override: SameAsApp unless the user chose otherwise.
[[nodiscard]] Language Chosen();
void Choose(Language language);
[[nodiscard]] rpl::producer<Language> ChosenValue();

// The language actually in use, never SameAsApp.
[[nodiscard]] Language Resolved();
[[nodiscard]] rpl::producer<> Changes();

// How a language calls itself, for the picker.
[[nodiscard]] QString Name(Language language);

// Called once from Core::Application before any session exists.
void Start();

} // namespace Fork::Lang
