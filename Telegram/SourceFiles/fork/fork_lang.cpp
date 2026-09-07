/*
This file is part of SeeGram Desktop,
a Telegram Desktop fork.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "fork/fork_lang.h"

#include "core/application.h"
#include "lang/lang_instance.h"

#include <rpl/event_stream.h>
#include <rpl/merge.h>

#include <QtCore/QFile>
#include <QtCore/QJsonDocument>
#include <QtCore/QJsonObject>
#include <QtCore/QSaveFile>

namespace Fork::Lang {
namespace {

constexpr auto kFileName = "tdata/fork_lang.json";

struct Locale { const char *id; const char *name; };
constexpr Locale Locales[] = {
	{ "en", "English" },
	{ "ru", "Русский" },
	{ "uk", "Українська" },
	{ "uz", "Oʻzbek" },
	{ "kk", "Қазақша" },
	{ "az", "Azərbaycan" },
	{ "es", "Español" },
	{ "pt", "Português" },
	{ "fr", "Français" },
	{ "de", "Deutsch" },
	{ "it", "Italiano" },
	{ "pl", "Polski" },
	{ "nl", "Nederlands" },
	{ "ro", "Română" },
	{ "cs", "Čeština" },
	{ "el", "Ελληνικά" },
	{ "tr", "Türkçe" },
	{ "ar", "العربية" },
	{ "fa", "فارسی" },
	{ "he", "עברית" },
	{ "hi", "हिन्दी" },
	{ "bn", "বাংলা" },
	{ "ur", "اردو" },
	{ "id", "Bahasa Indonesia" },
	{ "ms", "Bahasa Melayu" },
	{ "vi", "Tiếng Việt" },
	{ "th", "ไทย" },
	{ "zh", "中文" },
	{ "ja", "日本語" },
	{ "ko", "한국어" },
};
static_assert(std::size(Locales) == int(Language::Count) - 1);

constexpr auto Keys = std::array{
	"GhostMode",
	"DontSendReadReceipts",
	"DontSendTyping",
	"DontSendOnlineStatus",
	"DontSendUploadProgress",
	"GhostAbout",
	"SpyMode",
	"SpyEssentials",
	"SaveDeletedMessages",
	"SaveEditsHistory",
	"SaveForBots",
	"SpyAboutSaving",
	"SpyAboutBots",
	"Messages",
	"DeletedMark",
	"EditedMark",
	"TranslucentDeleted",
	"Reset",
	"MarksAbout",
	"SeeGramAbout",
	"Categories",
	"Links",
	"SourceCode",
	"NewsChannel",
	"LanguageTitle",
	"LanguageSameAsApp",
	"EditHistory",
	"DeletedMessages",
	"ViewDeleted",
	"ClearDeleted",
	"Clear",
	"Original",
	"Edited",
	"Current",
	"DeletedAt",
	"NoText",
	"NothingSaved",
	"ShowingLast",
	"ClearConfirm",
	"SeeTgTitle",
	"SeeTgEnabled",
	"SeeTgAbout",
	"SeeTgSignInAgain",
	"SeeTgSignedOut",
	"SeeTgTabTelegram",
	"SeeTgTabSeeTg",
	"SeeTgKindUpgraded",
	"SeeTgKindLimited",
	"SeeTgKindRegular",
	"SeeTgHiddenToggle",
	"SeeTgFilters",
	"SeeTgSort",
	"SeeTgSortNumberAsc",
	"SeeTgSortNumberDesc",
	"SeeTgSortName",
	"SeeTgSortEstimate",
	"SeeTgSortPrice",
	"SeeTgFilterCollection",
	"SeeTgFilterModel",
	"SeeTgFilterBackdrop",
	"SeeTgFilterPattern",
	"SeeTgFilterNumber",
	"SeeTgFilterOnSale",
	"SeeTgFilterHint",
	"SeeTgApply",
	"SeeTgLoading",
	"SeeTgEmpty",
	"SeeTgErrorAuth",
	"SeeTgErrorRate",
	"SeeTgErrorPremium",
	"SeeTgErrorNetwork",
	"SeeTgErrorOther",
	"SeeTgResolveTitle",
	"SeeTgResolveByGift",
	"SeeTgResolveByUsername",
	"SeeTgResolveFallback",
	"SeeTgResolveAbout",
	"SeeTgResolveAuto",
	"SeeTgWhoIs",
	"SeeTgWhoIsFailed",
	"SeeTgHistoryButton",
	"SeeTgHistoryTabNft",
	"SeeTgHistoryTabInfo",
	"SeeTgHistoryEmpty",
	"SeeTgHistoryError",
	"SeeTgHistoryLimitDay",
	"SeeTgHistoryLimitProfile",
	"SeeTgHistoryHiddenTitle",
	"SeeTgHistoryHiddenText",
	"SeeTgHistoryTransfersHiddenTitle",
	"SeeTgHistoryTransfersHiddenText",
	"SeeTgHistoryPremiumSub",
	"SeeTgHistoryHoldTitle",
	"SeeTgHistoryHoldSub",
	"SeeTgHistoryHoldSubBase",
	"SeeTgHistoryOpenApp",
	"SeeTgHistoryHiddenEvent",
	"SeeTgHistoryHiddenEventText",
	"SeeTgHistoryGiftSent",
	"SeeTgHistoryGiftReceived",
	"SeeTgHistoryGiftMoved",
	"SeeTgHistoryGiftHidden",
	"SeeTgHistoryGiftOpened",
	"SeeTgHistoryGiftUpgraded",
	"SeeTgHistorySavedGone",
	"SeeTgHistorySavedBack",
	"SeeTgHistorySavedGoneHint",
	"SeeTgHistorySavedGoneHidden",
	"SeeTgHistorySavedGoneUpgraded",
	"SeeTgHistorySavedGoneConverted",
	"SeeTgHistorySavedGift",
	"SeeTgHistoryFrom",
	"SeeTgHistoryTo",
	"SeeTgHistoryUnknownOwner",
	"SeeTgHistoryEmptyValue",
	"SeeTgHistoryFieldFirstName",
	"SeeTgHistoryFieldLastName",
	"SeeTgHistoryFieldPremium",
	"SeeTgHistoryFieldUsername",
	"SeeTgHistoryFieldUsernames",
	"SeeTgHistoryFieldTitle",
	"SeeTgHistoryFieldVerified",
	"SeeTgHistoryFieldDeleted",
	"SeeTgHistoryFieldScam",
	"SeeTgHistoryFieldFake",
	"SeeTgHistoryFromLabel",
	"SeeTgHistoryToLabel",
	"SeeTgHistoryDateLabel",
	"SeeTgHistoryGiftLabel",
	"SeeTgHistoryOpenGift",
	"SeeTgHistoryNewestFirst",
	"SeeTgHistoryOldestFirst",
	"SeeTgHistorySent",
	"SeeTgHistoryReceived",
	"SeeTgHistoryGone",
	"SeeTgHistoryProfileFields",
	"SeeTgFilterAny",
	"SeeTgPickCollectionFirst",
	"SeeTgHistoryMore",
	"SeeTgHistoryBuy",
	"SeeTgPremiumVerificationDescription",
	"SeeTgCommentBlocked",
	"SeeTgCommentsTab",
	"SeeTgCommentsEmpty",
	"SeeTgCommentsError",
	"SeeTgCommentsMore",
	"SeeTgCommentPlaceholder",
	"SeeTgCommentSend",
	"SeeTgCommentSendError",
	"SeeTgCommentActions",
	"SeeTgCommentPin",
	"SeeTgCommentUnpin",
	"SeeTgCommentDelete",
	"SeeTgCommentDeleteConfirm",
	"SeeTgCommentPinnedBadge",
	"SeeTgCommentJustNow",
	"SeeTgCommentFloodWait",
	"SeeTgCommentReply",
	"SeeTgCommentViewReplies",
	"SeeTgCommentHideReplies",
	"SeeTgCommentRepliesError",
	"SeeTgCommentReplyPlaceholder",
	"SeeTgCommentsLoadingMore",
	"SeeTgCommentsRetry",
	"SeeTgCommentsActionFailed",
	"SeeTgCommentUnitMin",
	"SeeTgCommentUnitSec",
	"SeeTgCommentOpenProfile",
	"SeeTgResolveAdvanced",
	"SeeTgCommentsRefresh",
	"SeeTgCommentsCancel",
};
static_assert(std::size(Keys) == int(Key::Count));

Language GlobalChosen = Language::SameAsApp;
rpl::event_stream<Language> GlobalChosenChanges;

[[nodiscard]] QString FilePath() {
	return cWorkingDir() + QString::fromLatin1(kFileName);
}

[[nodiscard]] const char *Id(Language language) {
	const auto index = int(language) - 1;
	return (index >= 0 && index < int(std::size(Locales)))
		? Locales[index].id : "app";
}

[[nodiscard]] Language FromId(const QString &id) {
	for (auto i = 1; i != int(Language::Count); ++i) {
		if (id == QLatin1String(Id(Language(i)))) {
			return Language(i);
		}
	}
	return Language::SameAsApp;
}

// The application's language pack id looks like "ru", "uk", "pt-br", or
// is empty for the built-in English. Only the part before the dash counts.
[[nodiscard]] Language FromApp() {
	const auto id = ::Lang::GetInstance().id().toLower().replace('_', '-');
	auto language = FromId(id.section(u'-', 0, 0));
	if (id == u"ua"_q) {
		language = Language::Ukrainian;
	}
	return (language == Language::SameAsApp) ? Language::English : language;
}

[[nodiscard]] const QJsonObject &Dictionary(Language language) {
	static auto dictionaries = QMap<int, QJsonObject>();
	const auto key = int(language);
	if (!dictionaries.contains(key)) {
		auto file = QFile(u":/seegram/langs/"_q + QLatin1String(Id(language)) + u".json"_q);
		dictionaries.insert(key, file.open(QIODevice::ReadOnly)
			? QJsonDocument::fromJson(file.readAll()).object() : QJsonObject());
	}
	return dictionaries[key];
}

} // namespace

QString Text(Key key) {
	Expects(key < Key::Count);

	const auto name = QLatin1String(Keys[int(key)]);
	const auto text = Dictionary(Resolved()).value(name).toString();
	return text.isEmpty() ? Dictionary(Language::English).value(name).toString() : text;
}

rpl::producer<QString> Value(Key key) {
	return rpl::single(Text(key)) | rpl::then(Changes() | rpl::map([=] {
		return Text(key);
	}));
}

Language Chosen() {
	return GlobalChosen;
}

void Choose(Language language) {
	if (language == GlobalChosen || language == Language::Count) {
		return;
	}
	GlobalChosen = language;
	GlobalChosenChanges.fire_copy(language);

	auto object = QJsonObject();
	object.insert(u"language"_q, QLatin1String(Id(language)));

	auto file = QSaveFile(FilePath());
	if (!file.open(QIODevice::WriteOnly)) {
		LOG(("Lang Error: cant write '%1'.").arg(FilePath()));
		return;
	}
	file.write(QJsonDocument(object).toJson(QJsonDocument::Indented));
	if (!file.commit()) {
		LOG(("Lang Error: cant commit '%1'.").arg(FilePath()));
	}
}

rpl::producer<Language> ChosenValue() {
	return rpl::single(GlobalChosen) | rpl::then(GlobalChosenChanges.events());
}

Language Resolved() {
	return (GlobalChosen == Language::SameAsApp) ? FromApp() : GlobalChosen;
}

QString ResolvedId() {
	return QLatin1String(Id(Resolved()));
}

rpl::producer<> Changes() {
	return rpl::merge(
		GlobalChosenChanges.events() | rpl::to_empty,
		::Lang::GetInstance().idChanges() | rpl::to_empty);
}

QString Name(Language language) {
	const auto index = int(language) - 1;
	return (index >= 0 && index < int(std::size(Locales)))
		? QString::fromUtf8(Locales[index].name) : Text(Key::LanguageSameAsApp);
}

void Start() {
	auto file = QFile(FilePath());
	if (!file.open(QIODevice::ReadOnly)) {
		return; // No file yet: follow the application.
	}
	const auto document = QJsonDocument::fromJson(file.readAll());
	if (!document.isObject()) {
		LOG(("Lang Error: '%1' is not a JSON object, ignoring it."
			).arg(FilePath()));
		return;
	}
	const auto value = document.object().value(u"language"_q);
	GlobalChosen = value.isString()
		? FromId(value.toString())
		: Language::SameAsApp;
}

} // namespace Fork::Lang
