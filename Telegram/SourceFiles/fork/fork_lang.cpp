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

struct Entry {
	const char *en = nullptr;
	const char *ru = nullptr;
	const char *uk = nullptr;
	const char *uz = nullptr;
};

// One row per Key, in Key's order. The static_assert below keeps the two in
// step; a missing translation falls back to English at lookup time.
constexpr Entry Table[] = {
	{ // GhostMode
		"Ghost mode",
		"Режим призрака",
		"Режим привида",
		"Arvoh rejimi",
	},
	{ // DontSendReadReceipts
		"Don't send read receipts",
		"Не отправлять отметки о прочтении",
		"Не надсилати позначки про прочитання",
		"O‘qilganlik belgilarini yubormaslik",
	},
	{ // DontSendTyping
		"Don't send typing status",
		"Не отправлять статус «печатает»",
		"Не надсилати статус «друкує»",
		"«Yozmoqda» holatini yubormaslik",
	},
	{ // DontSendOnlineStatus
		"Don't send online status",
		"Не отправлять статус «в сети»",
		"Не надсилати статус «у мережі»",
		"«Onlayn» holatini yubormaslik",
	},
	{ // DontSendUploadProgress
		"Don't send upload progress",
		"Не отправлять прогресс загрузки",
		"Не надсилати прогрес завантаження",
		"Yuklash jarayonini yubormaslik",
	},
	{ // GhostAbout
		"Read receipts and typing are invisible to others; a missing online "
		"status is noticeable. The switch in the side menu toggles all four "
		"at once.",
		"Отметки о прочтении и статус «печатает» собеседник не увидит; "
		"отсутствие статуса «в сети» заметно. Переключатель в боковом меню "
		"включает все четыре сразу.",
		"Позначки про прочитання і статус «друкує» співрозмовник не побачить; "
		"відсутність статусу «у мережі» помітна. Перемикач у бічному меню "
		"вмикає всі чотири одразу.",
		"O‘qilganlik belgilari va «yozmoqda» holati boshqalarga ko‘rinmaydi; "
		"«onlayn» holatining yo‘qligi seziladi. Yon menyudagi tugma "
		"to‘rttalasini birdan yoqadi.",
	},

	{ // SpyMode
		"Spy mode",
		"Режим шпиона",
		"Режим шпигуна",
		"Josus rejimi",
	},
	{ // SpyEssentials
		"Spy essentials",
		"Функции шпиона",
		"Функції шпигуна",
		"Josus funksiyalari",
	},
	{ // SaveDeletedMessages
		"Save deleted messages",
		"Сохранять удалённые сообщения",
		"Зберігати видалені повідомлення",
		"O‘chirilgan xabarlarni saqlash",
	},
	{ // SaveEditsHistory
		"Save edits history",
		"Сохранять историю правок",
		"Зберігати історію редагувань",
		"Tahrirlar tarixini saqlash",
	},
	{ // SaveForBots
		"Save in bot dialogs",
		"Сохранять в чатах с ботами",
		"Зберігати в чатах із ботами",
		"Botlar bilan chatlarda saqlash",
	},
	{ // SpyAboutSaving
		"Deleted messages stay in the chat with a mark; earlier texts of "
		"edited ones open from the message menu. Only text is kept: media is "
		"gone once the chat reloads.",
		"Удалённые остаются в чате с пометкой, прежние тексты правок "
		"открываются из меню сообщения. Хранится только текст: медиа "
		"пропадает после перезагрузки чата.",
		"Видалені залишаються в чаті з позначкою, попередні тексти редагувань "
		"відкриваються з меню повідомлення. Зберігається лише текст: медіа "
		"зникає після перезавантаження чату.",
		"O‘chirilganlar belgi bilan chatda qoladi, tahrirlarning oldingi "
		"matnlari xabar menyusidan ochiladi. Faqat matn saqlanadi: chat qayta "
		"yuklangach media yo‘qoladi.",
	},
	{ // SpyAboutBots
		"Bots rewrite and delete their messages constantly, so they are "
		"skipped by default.",
		"Боты постоянно переписывают и удаляют свои сообщения, поэтому по "
		"умолчанию пропускаются.",
		"Боти постійно переписують і видаляють свої повідомлення, тому типово "
		"пропускаються.",
		"Botlar xabarlarini doim qayta yozib, o‘chirib turadi, shuning uchun "
		"sukut bo‘yicha o‘tkazib yuboriladi.",
	},

	{ // Messages
		"Messages",
		"Сообщения",
		"Повідомлення",
		"Xabarlar",
	},
	{ // DeletedMark
		"Deleted mark",
		"Метка удалённого",
		"Позначка видаленого",
		"O‘chirilgan belgisi",
	},
	{ // EditedMark
		"Edited mark",
		"Метка «изменено»",
		"Позначка «змінено»",
		"«Tahrirlangan» belgisi",
	},
	{ // TranslucentDeleted
		"Translucent deleted messages",
		"Полупрозрачные удалённые сообщения",
		"Напівпрозорі видалені повідомлення",
		"Yarim shaffof o‘chirilgan xabarlar",
	},
	{ // Reset
		"Reset",
		"Сбросить",
		"Скинути",
		"Tiklash",
	},
	{ // MarksAbout
		"Empty means the default. Messages already on screen update as they "
		"are redrawn.",
		"Пустое поле — значение по умолчанию. Сообщения на экране обновятся "
		"при перерисовке.",
		"Порожнє поле — типове значення. Повідомлення на екрані оновляться "
		"під час перемальовування.",
		"Bo‘sh maydon — sukut qiymati. Ekrandagi xabarlar qayta chizilganda "
		"yangilanadi.",
	},

	{ // SeeGramAbout
		"Telegram Desktop that keeps quiet about you and remembers what "
		"others erase.",
		"Telegram Desktop, который молчит о вас и помнит то, что стирают "
		"другие.",
		"Telegram Desktop, який мовчить про вас і пам’ятає те, що стирають "
		"інші.",
		"Siz haqingizda jim turadigan va boshqalar o‘chirganini eslab "
		"qoladigan Telegram Desktop.",
	},
	{ // Categories
		"Categories",
		"Категории",
		"Категорії",
		"Toifalar",
	},
	{ // Links
		"Links",
		"Ссылки",
		"Посилання",
		"Havolalar",
	},
	{ // SourceCode
		"Source code",
		"Исходный код",
		"Початковий код",
		"Manba kodi",
	},
	{ // NewsChannel
		"News channel",
		"Новостной канал",
		"Канал новин",
		"Yangiliklar kanali",
	},
	{ // LanguageTitle
		"Language",
		"Язык",
		"Мова",
		"Til",
	},
	{ // LanguageSameAsApp
		"Same as the app",
		"Как в приложении",
		"Як у застосунку",
		"Ilova tili bilan bir xil",
	},

	{ // EditHistory
		"Edit history",
		"История правок",
		"Історія редагувань",
		"Tahrirlar tarixi",
	},
	{ // DeletedMessages
		"Deleted messages",
		"Удалённые сообщения",
		"Видалені повідомлення",
		"O‘chirilgan xabarlar",
	},
	{ // ViewDeleted
		"View deleted messages",
		"Показать удалённые",
		"Показати видалені",
		"O‘chirilganlarni ko‘rish",
	},
	{ // ClearDeleted
		"Clear deleted messages",
		"Очистить удалённые",
		"Очистити видалені",
		"O‘chirilganlarni tozalash",
	},
	{ // Clear
		"Clear",
		"Очистить",
		"Очистити",
		"Tozalash",
	},
	{ // Original
		"Original",
		"Исходное",
		"Початкове",
		"Asl",
	},
	{ // Edited
		"Edited",
		"Изменено",
		"Змінено",
		"Tahrirlangan",
	},
	{ // Current
		"Current",
		"Текущее",
		"Поточне",
		"Joriy",
	},
	{ // DeletedAt
		"deleted",
		"удалено",
		"видалено",
		"o‘chirilgan",
	},
	{ // NoText
		"(no text)",
		"(без текста)",
		"(без тексту)",
		"(matn yo‘q)",
	},
	{ // NothingSaved
		"No deleted messages have been saved in this chat yet.",
		"В этом чате ещё нет сохранённых удалённых сообщений.",
		"У цьому чаті ще немає збережених видалених повідомлень.",
		"Bu chatda saqlangan o‘chirilgan xabarlar hali yo‘q.",
	},
	{ // ShowingLast
		"Showing the last %1 of %2.",
		"Показаны последние %1 из %2.",
		"Показано останні %1 з %2.",
		"Oxirgi %1 tasi ko‘rsatilmoqda, jami %2.",
	},
	{ // ClearConfirm
		"Clear the deleted messages kept in this chat?",
		"Очистить сохранённые удалённые сообщения этого чата?",
		"Очистити збережені видалені повідомлення цього чату?",
		"Bu chatda saqlangan o‘chirilgan xabarlar tozalansinmi?",
	},
};

static_assert(std::size(Table) == int(Key::Count));

Language GlobalChosen = Language::SameAsApp;
rpl::event_stream<Language> GlobalChosenChanges;

[[nodiscard]] QString FilePath() {
	return cWorkingDir() + QString::fromLatin1(kFileName);
}

[[nodiscard]] const char *Id(Language language) {
	switch (language) {
	case Language::English: return "en";
	case Language::Russian: return "ru";
	case Language::Ukrainian: return "uk";
	case Language::Uzbek: return "uz";
	case Language::SameAsApp:
	case Language::Count: break;
	}
	return "app";
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
	const auto id = ::Lang::GetInstance().id().toLower();
	const auto base = id.section(u'-', 0, 0);
	return (base == u"ru"_q)
		? Language::Russian
		: (base == u"uk"_q || base == u"ua"_q)
		? Language::Ukrainian
		: (base == u"uz"_q)
		? Language::Uzbek
		: Language::English;
}

[[nodiscard]] const char *Pick(const Entry &entry, Language language) {
	const auto text = (language == Language::Russian)
		? entry.ru
		: (language == Language::Ukrainian)
		? entry.uk
		: (language == Language::Uzbek)
		? entry.uz
		: entry.en;
	return (text && *text) ? text : entry.en;
}

} // namespace

QString Text(Key key) {
	Expects(key < Key::Count);

	return QString::fromUtf8(Pick(Table[int(key)], Resolved()));
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

rpl::producer<> Changes() {
	return rpl::merge(
		GlobalChosenChanges.events() | rpl::to_empty,
		::Lang::GetInstance().idChanges() | rpl::to_empty);
}

QString Name(Language language) {
	switch (language) {
	case Language::English: return u"English"_q;
	case Language::Russian: return QString::fromUtf8("Русский");
	case Language::Ukrainian: return QString::fromUtf8("Українська");
	case Language::Uzbek: return QString::fromUtf8("O‘zbekcha");
	case Language::SameAsApp:
	case Language::Count: break;
	}
	return Text(Key::LanguageSameAsApp);
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
