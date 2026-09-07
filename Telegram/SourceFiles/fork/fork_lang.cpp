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

	{ // SeeTgTitle
		"see.tg integration",
		"Интеграция see.tg",
		"Інтеграція see.tg",
		"see.tg integratsiyasi",
	},
	{ // SeeTgEnabled
		"Enable see.tg",
		"Включить see.tg",
		"Увімкнути see.tg",
		"see.tg ni yoqish",
	},
	{ // SeeTgAbout
		"Adds a see.tg view to the gifts tab of a profile: NFT and regular "
		"gifts, hidden ones included, with filters and sorting. Signs in "
		"through @seetgbot on its own.",
		"Добавляет во вкладку подарков профиля режим see.tg: NFT и обычные "
		"подарки, включая скрытые, с фильтрами и сортировкой. Входит через "
		"@seetgbot сам.",
		"Додає у вкладку подарунків профілю режим see.tg: NFT і звичайні "
		"подарунки, зокрема приховані, з фільтрами та сортуванням. Входить "
		"через @seetgbot сам.",
		"Profil sovg‘alar bo‘limiga see.tg rejimini qo‘shadi: NFT va oddiy "
		"sovg‘alar, yashirinlari bilan, filtrlar va saralash. @seetgbot "
		"orqali o‘zi kiradi.",
	},
	{ // SeeTgSignInAgain
		"Sign in again",
		"Войти заново",
		"Увійти заново",
		"Qayta kirish",
	},
	{ // SeeTgSignedOut
		"Signed out of see.tg, the next request signs in again.",
		"Сессия see.tg сброшена, следующий запрос войдёт заново.",
		"Сесію see.tg скинуто, наступний запит увійде заново.",
		"see.tg sessiyasi tozalandi, keyingi so‘rov qayta kiradi.",
	},
	{ // SeeTgTabTelegram
		"Telegram",
		"Telegram",
		"Telegram",
		"Telegram",
	},
	{ // SeeTgTabSeeTg
		"see.tg",
		"see.tg",
		"see.tg",
		"see.tg",
	},
	{ // SeeTgKindUpgraded
		"Upgraded",
		"Улучшенные",
		"Покращені",
		"Yaxshilangan",
	},
	{ // SeeTgKindLimited
		"Non-upgraded",
		"Не улучшенные",
		"Не покращені",
		"Yaxshilanmagan",
	},
	{ // SeeTgKindRegular
		"Regular",
		"Обычные",
		"Звичайні",
		"Oddiy",
	},
	{ // SeeTgHiddenToggle
		"Hidden",
		"Скрытые",
		"Приховані",
		"Yashirin",
	},
	{ // SeeTgFilters
		"Filters",
		"Фильтры",
		"Фільтри",
		"Filtrlar",
	},
	{ // SeeTgSort
		"Sort",
		"Сортировка",
		"Сортування",
		"Saralash",
	},
	{ // SeeTgSortNumberAsc
		"By number, ascending",
		"По номеру, по возрастанию",
		"За номером, за зростанням",
		"Raqam bo‘yicha, o‘sish",
	},
	{ // SeeTgSortNumberDesc
		"By number, descending",
		"По номеру, по убыванию",
		"За номером, за спаданням",
		"Raqam bo‘yicha, kamayish",
	},
	{ // SeeTgSortName
		"By name",
		"По названию",
		"За назвою",
		"Nomi bo‘yicha",
	},
	{ // SeeTgSortEstimate
		"By estimate",
		"По оценке",
		"За оцінкою",
		"Baho bo‘yicha",
	},
	{ // SeeTgSortPrice
		"By price",
		"По цене",
		"За ціною",
		"Narx bo‘yicha",
	},
	{ // SeeTgFilterCollection
		"Collection",
		"Коллекция",
		"Колекція",
		"Kolleksiya",
	},
	{ // SeeTgFilterModel
		"Model",
		"Модель",
		"Модель",
		"Model",
	},
	{ // SeeTgFilterBackdrop
		"Backdrop",
		"Фон",
		"Тло",
		"Fon",
	},
	{ // SeeTgFilterPattern
		"Pattern",
		"Узор",
		"Візерунок",
		"Naqsh",
	},
	{ // SeeTgFilterNumber
		"Number, e.g. 1-100",
		"Номер, например 1-100",
		"Номер, наприклад 1-100",
		"Raqam, masalan 1-100",
	},
	{ // SeeTgFilterOnSale
		"Only on sale",
		"Только продающиеся",
		"Лише у продажу",
		"Faqat sotuvdagilar",
	},
	{ // SeeTgFilterHint
		"Names as on see.tg, several separated by commas.",
		"Названия как на see.tg, несколько через запятую.",
		"Назви як на see.tg, кілька через кому.",
		"Nomlar see.tg dagidek, bir nechtasi vergul bilan.",
	},
	{ // SeeTgApply
		"Apply",
		"Применить",
		"Застосувати",
		"Qo‘llash",
	},
	{ // SeeTgLoading
		"Loading…",
		"Загрузка…",
		"Завантаження…",
		"Yuklanmoqda…",
	},
	{ // SeeTgEmpty
		"Nothing here.",
		"Здесь пусто.",
		"Тут порожньо.",
		"Bu yerda hech narsa yo‘q.",
	},
	{ // SeeTgErrorAuth
		"Could not sign in to see.tg.",
		"Не удалось войти в see.tg.",
		"Не вдалося увійти до see.tg.",
		"see.tg ga kirib bo‘lmadi.",
	},
	{ // SeeTgErrorRate
		"Too many requests, try again in a minute.",
		"Слишком много запросов, попробуйте через минуту.",
		"Забагато запитів, спробуйте за хвилину.",
		"So‘rovlar ko‘p, bir daqiqadan so‘ng urinib ko‘ring.",
	},
	{ // SeeTgErrorPremium
		"This view needs see.tg or Telegram Premium.",
		"Этот раздел доступен с see.tg или Telegram Premium.",
		"Цей розділ доступний із see.tg або Telegram Premium.",
		"Bu bo‘lim see.tg yoki Telegram Premium bilan ochiladi.",
	},
	{ // SeeTgErrorNetwork
		"see.tg is unreachable.",
		"see.tg недоступен.",
		"see.tg недоступний.",
		"see.tg ga ulanib bo‘lmadi.",
	},
	{ // SeeTgErrorOther
		"see.tg returned an error.",
		"see.tg вернул ошибку.",
		"see.tg повернув помилку.",
		"see.tg xato qaytardi.",
	},
	{ // SeeTgResolveTitle
		"Peer lookup",
		"Поиск пользователей",
		"Пошук користувачів",
		"Foydalanuvchilarni qidirish",
	},
	{ // SeeTgResolveByGift
		"By gift, no limits",
		"По подарку, без лимитов",
		"За подарунком, без лімітів",
		"Sovg‘a bo‘yicha, cheklovsiz",
	},
	{ // SeeTgResolveByUsername
		"By username, up to 200 a day",
		"По username, до 200 в сутки",
		"За username, до 200 на добу",
		"Username bo‘yicha, kuniga 200 tagacha",
	},
	{ // SeeTgResolveFallback
		"Fall back to username",
		"Запасной путь через username",
		"Запасний шлях через username",
		"Username orqali zaxira yo‘l",
	},
	{ // SeeTgResolveAbout
		"see.tg knows a person's id but not the key Telegram needs to show "
		"them. By gift asks Telegram for any collectible the person holds and "
		"gets the key with it; by username spends the account's daily ration "
		"of lookups.",
		"see.tg знает id человека, но не ключ, без которого Telegram его не "
		"покажет. По подарку клиент запрашивает у Telegram любой NFT этого "
		"человека и получает ключ вместе с ним; по username расходуется "
		"дневной лимит запросов аккаунта.",
		"see.tg знає id людини, але не ключ, без якого Telegram її не покаже. "
		"За подарунком клієнт запитує в Telegram будь-який NFT цієї людини й "
		"отримує ключ разом із ним; за username витрачається добовий ліміт "
		"запитів облікового запису.",
		"see.tg odamning id sini biladi, ammo Telegram uni ko‘rsatishi uchun "
		"kerak kalitni emas. Sovg‘a bo‘yicha mijoz Telegramdan bu odamning "
		"istalgan NFT sini so‘raydi va kalitni u bilan oladi; username "
		"bo‘yicha akkauntning kunlik so‘rovlar limiti sarflanadi.",
	},
	{ // SeeTgResolveAuto
		"Find out the sender automatically",
		"Узнавать отправителя автоматически",
		"Дізнаватися відправника автоматично",
		"Yuboruvchini avtomatik aniqlash",
	},
	{ // SeeTgWhoIs
		"Who is it",
		"Узнать кто",
		"Дізнатися хто",
		"Kimligini bilish",
	},
	{ // SeeTgWhoIsFailed
		"Could not find out who that is.",
		"Не удалось узнать, кто это.",
		"Не вдалося дізнатися, хто це.",
		"Bu kimligini aniqlab bo‘lmadi.",
	},
	{ // SeeTgHistoryButton
		"History",
		"История",
		"Історія",
		"Tarix",
	},
	{ // SeeTgHistoryTabNft
		"NFT",
		"NFT",
		"NFT",
		"NFT",
	},
	{ // SeeTgHistoryTabInfo
		"Info",
		"Инфо",
		"Інфо",
		"Maʼlumot",
	},
	{ // SeeTgHistoryEmpty
		"No history yet",
		"История пуста",
		"Історія порожня",
		"Tarix boʻsh",
	},
	{ // SeeTgHistoryError
		"Couldn't load the history",
		"Не удалось загрузить историю",
		"Не вдалося завантажити історію",
		"Tarixni yuklab boʻlmadi",
	},
	{ // SeeTgHistoryLimitDay
		"Free requests are over for today. With see.tg Premium there is no "
		"limit.",
		"Бесплатные запросы на сегодня закончились. С see.tg Premium лимита "
		"нет.",
		"Безкоштовні запити на сьогодні закінчилися. З see.tg Premium ліміту "
		"немає.",
		"Bugungi bepul so‘rovlar tugadi. see.tg Premium bilan limit yo‘q.",
	},
	{ // SeeTgHistoryLimitProfile
		"You have opened this profile's history too many times today. With "
		"see.tg Premium there is no limit.",
		"Историю этого профиля вы сегодня открывали слишком часто. С see.tg "
		"Premium лимита нет.",
		"Історію цього профілю ви сьогодні відкривали надто часто. З see.tg "
		"Premium ліміту немає.",
		"Bu profil tarixini bugun juda ko‘p ochdingiz. see.tg Premium bilan "
		"cheklov yo‘q.",
	},
	{ // SeeTgHistoryHiddenTitle
		"History is hidden",
		"История скрыта",
		"Історію приховано",
		"Tarix yashirilgan",
	},
	{ // SeeTgHistoryHiddenText
		"The owner hides their history with see.tg Premium.",
		"Владелец скрыл историю передач с помощью see.tg Premium.",
		"Власник приховав історію передач за допомогою see.tg Premium.",
		"Egasi tarixini see.tg Premium yordamida yashirgan.",
	},
	{ // SeeTgHistoryTransfersHiddenTitle
		"Transfers are hidden",
		"Передачи скрыты",
		"Передачі приховано",
		"Oʻtkazmalar yashirilgan",
	},
	{ // SeeTgHistoryTransfersHiddenText
		"The owner hides their transfers from this profile's history.",
		"Владелец скрыл свои передачи из истории профиля.",
		"Власник приховав свої передачі з історії профілю.",
		"Egasi oʻz oʻtkazmalarini profil tarixidan yashirgan.",
	},
	{ // SeeTgHistoryPremiumSub
		"hides the whole history",
		"скрывает историю целиком",
		"приховує історію повністю",
		"butun tarixni yashiradi",
	},
	{ // SeeTgHistoryHoldTitle
		"Hold {m}",
		"Холд {m}",
		"Холд {m}",
		"Hold {m}",
	},
	{ // SeeTgHistoryHoldSub
		"hides transfers from the profile",
		"скрывает передачи из профиля",
		"приховує передачі з профілю",
		"profildan oʻtkazmalarni yashiradi",
	},
	{ // SeeTgHistoryHoldSubBase
		"basic hiding, transfers only",
		"базовое скрытие, только передачи",
		"базове приховування, лише передачі",
		"oddiy yashirish, faqat oʻtkazmalar",
	},
	{ // SeeTgHistoryOpenApp
		"Open in see.tg",
		"Открыть в see.tg",
		"Відкрити в see.tg",
		"see.tg da ochish",
	},
	{ // SeeTgHistoryHiddenEvent
		"Hidden event",
		"Событие скрыто",
		"Подію приховано",
		"Voqea yashirilgan",
	},
	{ // SeeTgHistoryHiddenEventText
		"A participant hides their history with see.tg Premium.",
		"Участник события скрыл свою историю с помощью see.tg Premium.",
		"Учасник події приховав свою історію за допомогою see.tg Premium.",
		"Ishtirokchi oʻz tarixini see.tg Premium yordamida yashirgan.",
	},
	{ // SeeTgHistoryGiftSent
		"Gift sent",
		"Подарок отправлен",
		"Подарунок надіслано",
		"Sovgʻa yuborildi",
	},
	{ // SeeTgHistoryGiftReceived
		"Gift received",
		"Подарок получен",
		"Подарунок отримано",
		"Sovgʻa olindi",
	},
	{ // SeeTgHistoryGiftMoved
		"Gift moved",
		"Подарок перемещён",
		"Подарунок переміщено",
		"Sovgʻa koʻchirildi",
	},
	{ // SeeTgHistoryGiftHidden
		"Gift hidden",
		"Подарок скрыт",
		"Подарунок приховано",
		"Sovgʻa yashirildi",
	},
	{ // SeeTgHistoryGiftOpened
		"Gift opened",
		"Подарок открыт",
		"Подарунок відкрито",
		"Sovgʻa ochildi",
	},
	{ // SeeTgHistoryGiftUpgraded
		"Gift upgraded",
		"Подарок улучшен",
		"Подарунок покращено",
		"Sovgʻa yaxshilandi",
	},
	{ // SeeTgHistorySavedGone
		"Gift left the profile",
		"Подарок пропал с профиля",
		"Подарунок зник з профілю",
		"Sovgʻa profildan yoʻqoldi",
	},
	{ // SeeTgHistorySavedBack
		"Gift is back on the profile",
		"Подарок вернулся на профиль",
		"Подарунок повернувся на профіль",
		"Sovgʻa profilga qaytdi",
	},
	{ // SeeTgHistorySavedGoneHint
		"Hidden, upgraded or converted to stars. Telegram does not say which.",
		"Скрыт, улучшен или обменян на звёзды. Telegram не сообщает, что "
		"именно.",
		"Прихований, покращений або обміняний на зірки. Telegram не "
		"повідомляє, що саме.",
		"Yashirilgan, yaxshilangan yoki yulduzlarga almashtirilgan. Telegram "
		"qaysi biri ekanini aytmaydi.",
	},
	{ // SeeTgHistorySavedGoneHidden
		"Hidden by the owner",
		"Скрыт владельцем",
		"Прихований власником",
		"Egasi yashirgan",
	},
	{ // SeeTgHistorySavedGoneUpgraded
		"Hidden or upgraded",
		"Скрыт или улучшен",
		"Прихований або покращений",
		"Yashirilgan yoki yaxshilangan",
	},
	{ // SeeTgHistorySavedGoneConverted
		"Hidden or converted to stars",
		"Скрыт или обменян на звёзды",
		"Прихований або обміняний на зірки",
		"Yashirilgan yoki yulduzlarga almashtirilgan",
	},
	{ // SeeTgHistorySavedGift
		"Regular gift",
		"Обычный подарок",
		"Звичайний подарунок",
		"Oddiy sovgʻa",
	},
	{ // SeeTgHistoryFrom
		"from",
		"от",
		"від",
		"kimdan",
	},
	{ // SeeTgHistoryTo
		"to",
		"кому",
		"кому",
		"kimga",
	},
	{ // SeeTgHistoryUnknownOwner
		"User",
		"Пользователь",
		"Користувач",
		"Foydalanuvchi",
	},
	{ // SeeTgHistoryEmptyValue
		"empty",
		"пусто",
		"порожньо",
		"boʻsh",
	},
	{ // SeeTgHistoryFieldFirstName
		"First name",
		"Имя",
		"Імʼя",
		"Ism",
	},
	{ // SeeTgHistoryFieldLastName
		"Last name",
		"Фамилия",
		"Прізвище",
		"Familiya",
	},
	{ // SeeTgHistoryFieldPremium
		"Premium",
		"Premium",
		"Premium",
		"Premium",
	},
	{ // SeeTgHistoryFieldUsername
		"Username",
		"Username",
		"Username",
		"Username",
	},
	{ // SeeTgHistoryFieldUsernames
		"Usernames",
		"Usernames",
		"Usernames",
		"Usernames",
	},
	{ // SeeTgHistoryFieldTitle
		"Title",
		"Название",
		"Назва",
		"Nomi",
	},
	{ // SeeTgHistoryFieldVerified
		"Verified",
		"Верификация",
		"Верифікація",
		"Verifikatsiya",
	},
	{ // SeeTgHistoryFieldDeleted
		"Deleted",
		"Удалён",
		"Видалений",
		"Oʻchirilgan",
	},
	{ // SeeTgHistoryFieldScam
		"Scam",
		"Scam",
		"Scam",
		"Scam",
	},
	{ // SeeTgHistoryFieldFake
		"Fake",
		"Fake",
		"Fake",
		"Fake",
	},
	{ // SeeTgHistoryFromLabel
		"From",
		"От",
		"Від",
		"Kimdan",
	},
	{ // SeeTgHistoryToLabel
		"To",
		"Кому",
		"Кому",
		"Kimga",
	},
	{ // SeeTgHistoryDateLabel
		"Date",
		"Дата",
		"Дата",
		"Sana",
	},
	{ // SeeTgHistoryGiftLabel
		"Gift",
		"Подарок",
		"Подарунок",
		"Sovgʻa",
	},
	{ // SeeTgHistoryOpenGift
		"Open gift",
		"Открыть подарок",
		"Відкрити подарунок",
		"Sovgʻani ochish",
	},
	{ // SeeTgHistoryNewestFirst
		"Newest first",
		"Сначала новые",
		"Спочатку нові",
		"Avval yangilari",
	},
	{ // SeeTgHistoryOldestFirst
		"Oldest first",
		"Сначала старые",
		"Спочатку старі",
		"Avval eskilari",
	},
	{ // SeeTgHistorySent
		"Sent",
		"Отправленные",
		"Надіслані",
		"Yuborilgan",
	},
	{ // SeeTgHistoryReceived
		"Received",
		"Полученные",
		"Отримані",
		"Olingan",
	},
	{ // SeeTgHistoryGone
		"Departures",
		"Пропажи",
		"Зникнення",
		"Yoʻqolishlar",
	},
	{ // SeeTgHistoryProfileFields
		"Profile",
		"Профиль",
		"Профіль",
		"Profil",
	},
	{ // SeeTgFilterAny
		"Any",
		"Любые",
		"Будь-які",
		"Istalgan",
	},
	{ // SeeTgPickCollectionFirst
		"Pick a collection first",
		"Сначала выберите коллекцию",
		"Спочатку виберіть колекцію",
		"Avval kolleksiyani tanlang",
	},
	{ // SeeTgHistoryMore
		"Details",
		"Подробнее",
		"Докладніше",
		"Batafsil",
	},
	{ // SeeTgHistoryBuy
		"Buy",
		"Купить",
		"Купити",
		"Sotib olish",
	},
	{
		"This account has a see.tg Premium subscription.",
		"У аккаунта есть подписка see.tg Premium.",
		"У акаунта є підписка see.tg Premium.",
		"Bu akkauntda see.tg Premium obunasi bor.",
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
