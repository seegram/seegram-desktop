/*
This file is part of SeeGram Desktop,
a Telegram Desktop fork.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "fork/seetg/seetg_history.h"
#include "fork/seetg/seetg_history_layout.h"

#include "fork/fork_lang.h"
#include "fork/seetg/seetg_profile_counters.h"
#include "fork/seetg/seetg_api.h"
#include "fork/seetg/seetg_card.h"
#include "fork/seetg/seetg_peers.h"
#include "fork/seetg/seetg_settings.h"
#include "fork/seetg/seetg_types.h"
#include "fork/seetg/seetg_visuals.h"
#include "api/api_premium.h"
#include "apiwrap.h"
#include "base/unixtime.h"
#include "boxes/star_gift_cover_box.h"
#include "chat_helpers/stickers_lottie.h"
#include "core/click_handler_types.h"
#include "core/local_url_handlers.h"
#include "data/data_peer.h"
#include "data/data_session.h"
#include "data/data_credits.h"
#include "data/data_document.h"
#include "data/data_document_media.h"
#include "data/data_file_origin.h"
#include "data/data_star_gift.h"
#include "data/data_user.h"
#include "info/info_controller.h"
#include "info/info_memento.h"
#include "lang/lang_keys.h"
#include "lottie/lottie_common.h"
#include "lottie/lottie_single_player.h"
#include "main/main_session.h"
#include "settings/settings_credits_graphics.h"
#include "ui/effects/credits_graphics.h"
#include "ui/controls/sub_tabs.h"
#include "ui/controls/table_rows.h"
#include "ui/layers/generic_box.h"
#include "ui/painter.h"
#include "ui/text/text.h"
#include "ui/ui_utility.h"
#include "ui/vertical_list.h"
#include "ui/widgets/buttons.h"
#include "ui/widgets/labels.h"
#include "ui/widgets/menu/menu_add_action_callback.h"
#include "ui/widgets/popup_menu.h"
#include "ui/wrap/slide_wrap.h"
#include "ui/wrap/table_layout.h"
#include "ui/wrap/vertical_layout.h"
#include "window/window_session_controller.h"
#include "styles/style_credits.h"
#include "styles/style_giveaway.h"
#include "styles/style_info.h"
#include "styles/style_layers.h"
#include "styles/style_media_player.h" // mediaPlayerMenuCheck
#include "styles/style_menu_icons.h"
#include "styles/style_premium.h"
#include "styles/style_settings.h"
#include "styles/style_widgets.h"

#include <QtCore/QJsonArray>
#include <QtCore/QJsonDocument>

namespace Fork::SeeTg::History {
namespace {

using Lang::Key;

constexpr auto kPageSize = 30;
constexpr auto kArtSize = 120;
constexpr auto kGlyphSize = 40;
constexpr auto kBlockMarginX = 10;
constexpr auto kBlockMarginY = 5;
constexpr auto kBlockPadding = 10;
constexpr auto kUserpicSize = 20;
constexpr auto kBoxCardSize = 160;
constexpr auto kRowPadding = 10;
constexpr auto kLoadMoreThreshold = 400;

// The mini app's own address for a profile: short digits resolve to the
// owner by telegram id. Everything the sheet cannot show itself - Premium,
// the hold perk - lives there.
constexpr auto kAppProfileUrl = "https://t.me/seetgbot/profile?startapp=%1";
constexpr auto kAppUrl = "https://t.me/seetgbot/app";
// Deep links the mini app understands from this client: startapp is limited
// to [A-Za-z0-9_-], so the hold model travels base64url-encoded.
constexpr auto kAppPremiumUrl = "https://t.me/seetgbot/app?startapp=tg_premium";
constexpr auto kAppHoldUrl = "https://t.me/seetgbot/app?startapp=tg_hold_%1";

enum class Tab {
	Nft,
	Saved,
	Info,
};

// The history documents are split by tab, as in the mini app: one document
// carrying both the NFT and the regular-gift selections sits at the server's
// complexity ceiling.
const char kDocNft[] =
	"query ProfileHistory($seeId: String!, $first: Int!, $after: String, "
	"$sortDir: SortDirection, $includeSent: Boolean!, $includeReceived: Boolean!, "
	"$includeSavedGifts: Boolean, $savedSent: Boolean, $savedReceived: Boolean, "
	"$includeSavedVisibility: Boolean, $profileFields: [String!]) { "
	"profileHistory(seeId: $seeId, first: $first, after: $after, sortDir: $sortDir, "
	"includeSent: $includeSent, includeReceived: $includeReceived, "
	"includeSavedGifts: $includeSavedGifts, savedSent: $savedSent, "
	"savedReceived: $savedReceived, includeSavedVisibility: $includeSavedVisibility, "
	"profileFields: $profileFields) { "
	"hiddenByOwner transfersHiddenByOwner "
	"items { id kind time hidden "
	"giftAction { action direction visibilityChange "
	"from { seeId telegramId telegramType name username } "
	"to { seeId telegramId telegramType name username } "
	"gift { id giftId title slug num model { name } backdrop { name } pattern { name } } } "
	"profileChange { field oldValue newValue } } "
	"pageInfo { endCursor hasNextPage } } }";

const char kDocSaved[] =
	"query ProfileHistorySaved($seeId: String!, $first: Int!, $after: String, "
	"$sortDir: SortDirection, $includeSent: Boolean!, $includeReceived: Boolean!, "
	"$includeSavedGifts: Boolean, $savedSent: Boolean, $savedReceived: Boolean, "
	"$includeSavedVisibility: Boolean, $profileFields: [String!]) { "
	"profileHistory(seeId: $seeId, first: $first, after: $after, sortDir: $sortDir, "
	"includeSent: $includeSent, includeReceived: $includeReceived, "
	"includeSavedGifts: $includeSavedGifts, savedSent: $savedSent, "
	"savedReceived: $savedReceived, includeSavedVisibility: $includeSavedVisibility, "
	"profileFields: $profileFields) { "
	"hiddenByOwner transfersHiddenByOwner "
	"items { id kind time hidden "
	"savedGiftAction { direction "
	"from { seeId telegramId telegramType name username } "
	"to { seeId telegramId telegramType name username } "
	"savedGift { id giftId stars issuedAt message } } "
	"savedVisibilityAction { change maybeConverted maybeUpgraded "
	"savedGift { id giftId stars issuedAt message } } } "
	"pageInfo { endCursor hasNextPage } } }";

// The viewer's own hold-perk status: which models grant hiding transfers.
// Asked only when a profile turns out to hide something.
// see.tg's own names for the regular-gift collections (the curated
// gift_names table); asked once per process, the first time a regular
// gift's sheet opens.
const char kDocSavedNames[] =
	"query SavedNames { savedGiftCollections { giftId name } }";

const char kDocHold[] =
	"query HoldHide { holdHide { available models { slug title model collectionId } } }";

const auto kProfileFields = std::array{
	u"first_name"_q,
	u"last_name"_q,
	u"premium"_q,
	u"username"_q,
	u"usernames"_q,
	u"title"_q,
	u"verified"_q,
	u"deleted"_q,
	u"scam"_q,
	u"fake"_q,
};

struct Event {
	enum class Type {
		Hidden,
		Gift,
		SavedGift,
		SavedVisibility,
		ProfileChange,
	};
	Type type = Type::Hidden;
	QString id;
	QString eventTitle;
	QStringList eventDetails;
	QString glyph;
	QString market;
	bool compact = false;
	TimeId time = 0;

	// Gift (collectible) action.
	QString action;
	bool sent = false;
	QString visibilityChange;
	Owner from;
	Owner to;
	uint64 giftId = 0;
	QString title;
	QString slug;
	int num = 0;
	QString model;
	QString backdrop;
	QString pattern;

	// Regular gift.
	uint64 savedGiftId = 0;
	int stars = 0;
	QString message;
	QString change;
	bool maybeConverted = false;
	bool maybeUpgraded = false;

	// Profile field change.
	QString field;
	QString oldValue;
	QString newValue;
};

struct PageData {
	bool hiddenByOwner = false;
	bool transfersHidden = false;
	std::vector<Event> items;
	QString cursor;
	bool hasNext = false;
};

struct HoldModel {
	QString slug;
	QString title;
	QString model;
	uint64 collectionId = 0;
};

[[nodiscard]] uint64 ToId(const QJsonValue &value) {
	return value.isString()
		? value.toString().toULongLong()
		: uint64(value.toDouble());
}

[[nodiscard]] Event ParseEvent(const QJsonObject &o) {
	auto result = Event();
	result.id = o.value(u"id"_q).toString();
	result.time = ParseTime(o.value(u"time"_q).toString());
	if (o.value(u"hidden"_q).toBool()) {
		result.type = Event::Type::Hidden;
		return result;
	}
	const auto gift = o.value(u"giftAction"_q).toObject();
	const auto saved = o.value(u"savedGiftAction"_q).toObject();
	const auto visibility = o.value(u"savedVisibilityAction"_q).toObject();
	const auto change = o.value(u"profileChange"_q).toObject();
	if (!gift.isEmpty()) {
		result.type = Event::Type::Gift;
		result.action = gift.value(u"action"_q).toString();
		result.sent = (gift.value(u"direction"_q).toString() == u"SENT"_q);
		result.visibilityChange = gift.value(u"visibilityChange"_q).toString();
		result.from = ParseOwner(gift.value(u"from"_q).toObject());
		result.to = ParseOwner(gift.value(u"to"_q).toObject());
		const auto g = gift.value(u"gift"_q).toObject();
		result.giftId = ToId(g.value(u"giftId"_q));
		result.title = g.value(u"title"_q).toString();
		result.slug = g.value(u"slug"_q).toString();
		result.num = g.value(u"num"_q).toInt();
		result.model = g.value(u"model"_q).toObject().value(u"name"_q).toString();
		result.backdrop = g.value(u"backdrop"_q).toObject().value(u"name"_q).toString();
		result.pattern = g.value(u"pattern"_q).toObject().value(u"name"_q).toString();
	} else if (!saved.isEmpty()) {
		result.type = Event::Type::SavedGift;
		result.sent = (saved.value(u"direction"_q).toString() == u"SENT"_q);
		result.from = ParseOwner(saved.value(u"from"_q).toObject());
		result.to = ParseOwner(saved.value(u"to"_q).toObject());
		const auto g = saved.value(u"savedGift"_q).toObject();
		result.savedGiftId = ToId(g.value(u"giftId"_q));
		result.stars = g.value(u"stars"_q).toInt();
		result.message = g.value(u"message"_q).toString();
	} else if (!visibility.isEmpty()) {
		result.type = Event::Type::SavedVisibility;
		result.change = visibility.value(u"change"_q).toString();
		result.maybeConverted = visibility.value(u"maybeConverted"_q).toBool();
		result.maybeUpgraded = visibility.value(u"maybeUpgraded"_q).toBool();
		const auto g = visibility.value(u"savedGift"_q).toObject();
		result.savedGiftId = ToId(g.value(u"giftId"_q));
		result.stars = g.value(u"stars"_q).toInt();
		result.message = g.value(u"message"_q).toString();
	} else {
		result.type = Event::Type::ProfileChange;
		result.field = change.value(u"field"_q).toString();
		result.oldValue = change.value(u"oldValue"_q).toString();
		result.newValue = change.value(u"newValue"_q).toString();
	}
	return result;
}

[[nodiscard]] PageData ParsePage(const QJsonObject &conn) {
	auto result = PageData();
	result.hiddenByOwner = conn.value(u"hiddenByOwner"_q).toBool();
	result.transfersHidden = conn.value(u"transfersHiddenByOwner"_q).toBool();
	for (const auto &item : conn.value(u"items"_q).toArray()) {
		result.items.push_back(ParseEvent(item.toObject()));
	}
	const auto page = conn.value(u"pageInfo"_q).toObject();
	result.cursor = page.value(u"endCursor"_q).toString();
	result.hasNext = page.value(u"hasNextPage"_q).toBool();
	return result;
}

// What the ⋮ menu lets the viewer narrow, per tab, like the mini app.
struct Filters {
	bool newestFirst = true;
	bool sent = true;
	bool received = true;
	bool savedSent = true;
	bool savedReceived = true;
	bool savedVisibility = true;
	base::flat_set<QString> hiddenFields;
};

[[nodiscard]] QJsonObject VariablesFor(
		Tab tab,
		const QString &seeId,
		const QString &after,
		const Filters &filters) {
	auto v = QJsonObject();
	v.insert(u"seeId"_q, seeId);
	v.insert(u"first"_q, kPageSize);
	v.insert(u"after"_q, after.isEmpty() ? QJsonValue() : QJsonValue(after));
	v.insert(u"sortDir"_q, filters.newestFirst ? u"DESC"_q : u"ASC"_q);
	const auto nft = (tab == Tab::Nft);
	const auto saved = (tab == Tab::Saved);
	const auto info = (tab == Tab::Info);
	v.insert(u"includeSent"_q, nft && filters.sent);
	v.insert(u"includeReceived"_q, nft && filters.received);
	v.insert(u"includeSavedGifts"_q, saved);
	v.insert(u"savedSent"_q, saved && filters.savedSent);
	v.insert(u"savedReceived"_q, saved && filters.savedReceived);
	v.insert(u"includeSavedVisibility"_q, saved && filters.savedVisibility);
	auto fields = QJsonArray();
	if (info) {
		for (const auto &field : kProfileFields) {
			if (!filters.hiddenFields.contains(field)) {
				fields.push_back(field);
			}
		}
	}
	v.insert(u"profileFields"_q, fields);
	return v;
}

base::flat_map<uint64, QString> SavedNames;
bool SavedNamesLoaded = false;

[[nodiscard]] QString GiftName(uint64 giftId) {
	const auto i = SavedNames.find(giftId);
	return (i != end(SavedNames)) ? i->second : QString();
}

void EnsureNames(not_null<Main::Session*> session, Fn<void()> done) {
	if (SavedNamesLoaded) {
		done();
		return;
	}
	Api::Query(
		session,
		QString::fromLatin1(kDocSavedNames),
		QJsonObject(),
		[=](const QJsonObject &data) {
			for (const auto &item : data.value(u"savedGiftCollections"_q).toArray()) {
				const auto o = item.toObject();
				const auto id = ToId(o.value(u"giftId"_q));
				const auto name = o.value(u"name"_q).toString();
				if (id && !name.isEmpty()) {
					SavedNames[id] = name;
				}
			}
			SavedNamesLoaded = true;
			done();
		},
		[=](const Api::Error &) {
			done();
		});
}

[[nodiscard]] QString OwnerName(const Owner &owner) {
	return owner.name.isEmpty()
		? (owner.username.isEmpty()
			? Lang::Text(Key::SeeTgHistoryUnknownOwner)
			: '@' + owner.username)
		: owner.name;
}

[[nodiscard]] QString FieldLabel(const QString &field) {
	static const auto Labels = base::flat_map<QString, Key>{
		{ u"first_name"_q, Key::SeeTgHistoryFieldFirstName },
		{ u"last_name"_q, Key::SeeTgHistoryFieldLastName },
		{ u"premium"_q, Key::SeeTgHistoryFieldPremium },
		{ u"username"_q, Key::SeeTgHistoryFieldUsername },
		{ u"usernames"_q, Key::SeeTgHistoryFieldUsernames },
		{ u"title"_q, Key::SeeTgHistoryFieldTitle },
		{ u"verified"_q, Key::SeeTgHistoryFieldVerified },
		{ u"deleted"_q, Key::SeeTgHistoryFieldDeleted },
		{ u"scam"_q, Key::SeeTgHistoryFieldScam },
		{ u"fake"_q, Key::SeeTgHistoryFieldFake },
	};
	const auto i = Labels.find(field);
	return (i != end(Labels)) ? Lang::Text(i->second) : field;
}

// Collectible usernames arrive as a JSON array or a comma list; the row shows
// what was added and what was dropped, not two long lists.
[[nodiscard]] QStringList Usernames(const QString &value) {
	auto result = QStringList();
	const auto trimmed = value.trimmed();
	if (trimmed.isEmpty()) {
		return result;
	} else if (trimmed.startsWith('[')) {
		const auto array = QJsonDocument::fromJson(trimmed.toUtf8()).array();
		for (const auto &item : array) {
			const auto name = item.isString()
				? item.toString()
				: item.toObject().value(u"username"_q).toString();
			if (!name.isEmpty()) {
				result.push_back(name);
			}
		}
	} else {
		for (const auto &part : trimmed.split(',')) {
			const auto name = part.trimmed();
			if (!name.isEmpty()) {
				result.push_back(name);
			}
		}
	}
	for (auto &name : result) {
		if (name.startsWith('@')) {
			name = name.mid(1);
		}
	}
	return result;
}

[[nodiscard]] QString ValueText(const QString &value) {
	if (value.isEmpty()) {
		return Lang::Text(Key::SeeTgHistoryEmptyValue);
	} else if (value == u"true"_q) {
		return QString::fromUtf8("\xe2\x9c\x93");
	} else if (value == u"false"_q) {
		return QString::fromUtf8("\xe2\x80\x94");
	}
	return value;
}

[[nodiscard]] QString GoneHint(const Event &e) {
	if (e.maybeConverted && e.maybeUpgraded) {
		return Lang::Text(Key::SeeTgHistorySavedGoneHint);
	} else if (e.maybeUpgraded) {
		return Lang::Text(Key::SeeTgHistorySavedGoneUpgraded);
	} else if (e.maybeConverted) {
		return Lang::Text(Key::SeeTgHistorySavedGoneConverted);
	}
	return Lang::Text(Key::SeeTgHistorySavedGoneHidden);
}

struct RowText {
	QString title;
	QString shortTitle;
	QStringList meta;
};

[[nodiscard]] RowText TextFor(const Event &e) {
	auto result = RowText();
	if (!e.eventTitle.isEmpty()) return { e.eventTitle, QString(), e.eventDetails };
	switch (e.type) {
	case Event::Type::Hidden:
		result.title = Lang::Text(Key::SeeTgHistoryHiddenEvent);
		result.meta.push_back(Lang::Text(Key::SeeTgHistoryHiddenEventText));
		break;
	case Event::Type::Gift: {
		const auto upgrade = (e.action == u"new"_q);
		result.title = (e.visibilityChange == u"hidden"_q)
			? Lang::Text(Key::SeeTgHistoryGiftHidden)
			: (e.visibilityChange == u"opened"_q)
			? Lang::Text(Key::SeeTgHistoryGiftOpened)
			: upgrade
			? Lang::Text(Key::SeeTgHistoryGiftUpgraded)
			: (e.action == u"moved"_q)
			? Lang::Text(Key::SeeTgHistoryGiftMoved)
			: e.sent
			? Lang::Text(Key::SeeTgHistoryGiftSent)
			: Lang::Text(Key::SeeTgHistoryGiftReceived);
		if (e.action == u"moved"_q
			&& e.visibilityChange != u"hidden"_q
			&& e.visibilityChange != u"opened"_q) {
			result.shortTitle = Lang::Text(Key::SeeTgHistoryGiftMovedShort);
		}
		break;
	}
	case Event::Type::SavedGift:
		result.title = e.sent
			? Lang::Text(Key::SeeTgHistoryGiftSent)
			: Lang::Text(Key::SeeTgHistoryGiftReceived);
		break;
	case Event::Type::SavedVisibility: {
		const auto gone = (e.change == u"gone"_q);
		result.title = gone
			? Lang::Text(Key::SeeTgHistorySavedGone)
			: Lang::Text(Key::SeeTgHistorySavedBack);
		if (gone) {
			result.meta.push_back(GoneHint(e));
		}
		break;
	}
	case Event::Type::ProfileChange: {
		result.title = FieldLabel(e.field);
		if (e.field == u"usernames"_q) {
			const auto before = Usernames(e.oldValue);
			const auto after = Usernames(e.newValue);
			auto parts = QStringList();
			for (const auto &name : after) {
				if (!before.contains(name)) {
					parts.push_back("+ @" + name);
				}
			}
			for (const auto &name : before) {
				if (!after.contains(name)) {
					parts.push_back(QString::fromUtf8("\xe2\x88\x92 @") + name);
				}
			}
			result.meta.push_back(parts.isEmpty()
				? Lang::Text(Key::SeeTgHistoryEmptyValue)
				: parts.join("  "));
		} else {
			result.meta.push_back(ValueText(e.oldValue)
				+ QString::fromUtf8(" \xe2\x86\x92 ")
				+ ValueText(e.newValue));
		}
		break;
	}
	}
	return result;
}

// The rows read at a size up from the client's list text: the mini app's
// tape is roomier than a chat list, and the card next to it is tall.
[[nodiscard]] const style::internal::OwnedFont &TitleFont() {
	static const auto result = style::internal::OwnedFont(
		st::semiboldFont->f.family(),
		st::semiboldFont->flags(),
		st::semiboldFont->f.pixelSize() + style::ConvertScale(2));
	return result;
}

[[nodiscard]] const style::internal::OwnedFont &LineFont() {
	static const auto result = style::internal::OwnedFont(
		st::normalFont->f.family(),
		st::normalFont->flags(),
		st::normalFont->f.pixelSize() + style::ConvertScale(1));
	return result;
}

[[nodiscard]] CardData CardFor(const Event &e) {
	if (e.type == Event::Type::Gift) {
		return {
			.giftId = e.giftId,
			.title = e.title,
			.model = e.model,
			.backdrop = e.backdrop,
			.pattern = e.pattern,
			.num = e.num,
		};
	}
	return { .giftId = e.savedGiftId };
}

[[nodiscard]] bool HasCard(const Event &e) {
	return (e.type == Event::Type::Gift)
		|| (e.type == Event::Type::SavedGift)
		|| (e.type == Event::Type::SavedVisibility);
}

// A participant as see.tg names them: the public picture by username and
// the name, with no lookup spent. Nobody is asked about until the event's
// own sheet opens.
class OwnerLine final {
public:
	OwnerLine(not_null<Ui::RpWidget*> widget, not_null<Main::Session*> session, const QString &caption, Owner owner);

	void paint(Painter &p, int x, int y, int available, int outerWidth) const;

private:
	QString _caption;
	Owner _owner;
	QString _name;
	QImage _userpic;
	PeerData *_peer = nullptr;
	mutable Ui::PeerUserpicView _nativeUserpic;

};

OwnerLine::OwnerLine(not_null<Ui::RpWidget*> widget, not_null<Main::Session*> session, const QString &caption, Owner owner)
: _caption(caption)
, _owner(std::move(owner))
, _name(OwnerName(_owner)) {
	_peer = _owner.known() ? session->data().peerLoaded(_owner.peerId) : nullptr;
	if (_peer) {
		if (!_peer->username().isEmpty()) {
			_owner.username = _peer->username();
		}
		_nativeUserpic = _peer->createUserpicView();
		_peer->loadUserpic();
		session->downloaderTaskFinished() | rpl::on_next([widget] {
			widget->update();
		}, widget->lifetime());
	}
	if (!_owner.username.isEmpty()) {
		Visuals::Image(
			Visuals::UserpicUrl(_owner.username),
			crl::guard(widget, [=, widget = widget.get()](QImage image) {
				_userpic = std::move(image);
				widget->update();
			}));
	}
}

void OwnerLine::paint(
		Painter &p,
		int x,
		int y,
		int available,
		int outerWidth) const {
	if (available <= 0) {
		return;
	}
	const auto startX = x;
	const auto &font = LineFont();
	const auto size = std::min(style::ConvertScale(kUserpicSize), available);
	const auto captionWidth = std::min(font->width(_caption),
		std::max(0, available - size - 2 * font->spacew - font->width(u"…"_q)));
	p.setFont(font->f);
	p.setPen(st::windowSubTextFg);
	if (captionWidth > 0) {
		p.drawTextLeft(x, y, outerWidth, font->elided(_caption, captionWidth));
		x += captionWidth + font->spacew;
	}
	const auto top = y + (font->height - size) / 2;
	const auto avatar = style::rtlrect(x, top, size, size, outerWidth);
	if (_userpic.isNull() && _peer && _peer->hasUserpic()) {
		_peer->paintUserpicLeft(p, _nativeUserpic, x, top, outerWidth, size, true);
	} else {
		auto hq = PainterHighQualityEnabler(p);
		auto path = QPainterPath();
		path.addEllipse(avatar);
		if (_userpic.isNull()) {
			p.setPen(Qt::NoPen);
			p.setBrush(st::windowBgOver);
			p.drawPath(path);
			p.setPen(st::windowSubTextFg);
			auto letterFont = st::semiboldFont->f;
			letterFont.setPixelSize(std::max(1, size * 3 / 5));
			p.setFont(letterFont);
			p.drawText(avatar, _name.left(1).toUpper(), style::al_center);
		} else {
			p.save();
			p.setClipPath(path, Qt::IntersectClip);
			p.drawImage(avatar, _userpic);
			p.restore();
		}
	}
	x += size + font->spacew;
	p.setFont(font->f);
	p.setPen(st::windowFg);
	p.drawTextLeft(x, y, outerWidth, font->elided(_name, std::max(0, available - (x - startX))));
}

// One event, drawn the way the mini app draws it: the gift's card on the
// left, what happened on the right, the time underneath.
class Row final : public Ui::AbstractButton {
public:
	Row(QWidget *parent, not_null<Main::Session*> session, Event event, std::optional<RowText> text = std::nullopt);

protected:
	void paintEvent(QPaintEvent *e) override;
	int resizeGetHeight(int newWidth) override;

private:
	void paintGlyph(Painter &p, QRect rect);

	Event _event;
	Card *_card = nullptr;
	QString _title;
	QString _shortTitle;
	QStringList _meta;
	// Each line is reached from an HTTP callback by address, so the lines
	// must never move: a growing vector of values did exactly that.
	std::vector<std::unique_ptr<OwnerLine>> _owners;
	QString _time;
	QImage _marketLogo;
	RowGeometry _geometry;

};

Row::Row(QWidget *parent, not_null<Main::Session*> session, Event event, std::optional<RowText> overrideText)
: AbstractButton(parent)
, _event(std::move(event)) {
	const auto text = overrideText.value_or(TextFor(_event));
	_title = text.title;
	_shortTitle = text.shortTitle;
	_meta = text.meta;
	const auto withPeers = (_event.type == Event::Type::Gift
		&& _event.action != u"new"_q)
		|| (_event.type == Event::Type::SavedGift);
	if (withPeers) {
		if (_event.from.known() || !_event.from.name.isEmpty()) {
			_owners.push_back(std::make_unique<OwnerLine>(
				this, session,
				Lang::Text(Key::SeeTgHistoryFrom) + ':',
				_event.from));
		}
		if (_event.to.known() || !_event.to.name.isEmpty()) {
			_owners.push_back(std::make_unique<OwnerLine>(
				this, session,
				Lang::Text(Key::SeeTgHistoryTo) + ':',
				_event.to));
		}
	}
	_time = _event.time
		? langDateTime(base::unixtime::parse(_event.time))
		: QString();
	if (HasCard(_event) && !_event.compact) {
		_card = Ui::CreateChild<Card>(this, CardFor(_event));
		_card->resize(kArtSize, kArtSize);
		_card->setAttribute(Qt::WA_TransparentForMouseEvents);
		_card->show();
	}
	if (!_event.market.isEmpty()) {
		const auto username = _event.market == u"mrkt" ? u"mrkt"_q
			: _event.market == u"portals" ? u"portals"_q
			: _event.market == u"tonnel" ? u"tonnel_relayer_bot"_q
			: _event.market == u"getgems" ? u"getgems"_q : QString();
		if (!username.isEmpty()) Visuals::Image(Visuals::UserpicUrl(username), crl::guard(this, [=](QImage image) {
			_marketLogo = std::move(image); update();
		}));
	}
	setPointerCursor(HasCard(_event));
}

int Row::resizeGetHeight(int newWidth) {
	const auto lines = 1 + int(_meta.size()) + int(_owners.size()) + (_time.isEmpty() ? 0 : 1);
	const auto lineHeight = std::max(LineFont()->height, style::ConvertScale(kUserpicSize));
	const auto textHeight = TitleFont()->height + (lines - 1) * (lineHeight + style::ConvertScale(4));
	_geometry = ComputeRowGeometry(newWidth,
		style::ConvertScale(_card ? kArtSize : kGlyphSize),
		style::ConvertScale(_card ? 64 : kGlyphSize), style::ConvertScale(160),
		textHeight, style::ConvertScale(kBlockPadding), style::ConvertScale(kBlockPadding + 4),
		style::ConvertScale(kBlockMarginX), style::ConvertScale(kBlockMarginY));
	if (_card) {
		_card->setGeometry(style::rtlrect(_geometry.art, newWidth));
	}
	return _geometry.height;
}

void Row::paintGlyph(Painter &p, QRect rect) {
	auto hq = PainterHighQualityEnabler(p);
	if (_event.compact) {
		// Match gift-history-row__mark in the miniapp: a 42px tinted ring,
		// a 24px coloured centre, and a white 19px extra-bold glyph.
		const auto mix = [](QColor a, QColor b, double weight) {
			return QColor::fromRgbF(a.redF() * weight + b.redF() * (1 - weight),
				a.greenF() * weight + b.greenF() * (1 - weight),
				a.blueF() * weight + b.blueF() * (1 - weight));
		};
		const auto accent = st::windowActiveTextFg->c;
		const auto kind = _event.action;
		const auto green = kind == u"listing" || !_event.market.isEmpty();
		const auto base = green ? QColor("#33c46a")
			: kind == u"delisting" ? QColor("#ff4d4d")
			: kind == u"price" ? QColor("#f0a400") : accent;
		const auto whiteWeight = kind == u"delisting" || kind == u"mint" ? .78
			: green || kind == u"price" ? .82 : .88;
		const auto tint = kind == u"price" ? .13 : kind == u"mint" ? .10 : .12;
		const auto scale = rect.width() / 42.;
		const auto bounds = QRectF(rect);
		p.setPen(Qt::NoPen);
		p.setBrush(mix(base, st::windowBg->c, tint));
		p.drawEllipse(bounds);
		if (!_marketLogo.isNull()) {
			auto path = QPainterPath(); path.addEllipse(bounds);
			p.save(); p.setClipPath(path); p.drawImage(rect, _marketLogo); p.restore();
		} else {
			auto centre = QRadialGradient(bounds.center(), 13 * scale);
			const auto fill = mix(base, QColor(Qt::white), whiteWeight);
			centre.setColorAt(0, fill);
			centre.setColorAt(12. / 13., fill);
			auto transparent = fill; transparent.setAlpha(0);
			centre.setColorAt(1, transparent);
			p.setBrush(centre);
			p.drawEllipse(bounds.center(), 13 * scale, 13 * scale);
			auto outline = accent; outline.setAlphaF(.22);
			p.setBrush(Qt::NoBrush); p.setPen(QPen(outline, scale));
			p.drawEllipse(bounds.adjusted(scale / 2, scale / 2, -scale / 2, -scale / 2));
			auto sheen = QLinearGradient(bounds.topLeft(), bounds.bottomLeft());
			sheen.setColorAt(0, QColor(255, 255, 255, 36));
			sheen.setColorAt(.5, QColor(255, 255, 255, 0));
			p.setPen(QPen(QBrush(sheen), scale));
			p.drawEllipse(bounds.adjusted(scale / 2, scale / 2, -scale / 2, -scale / 2));
			auto font = st::semiboldFont->f;
			font.setPixelSize(int(std::round(19 * scale)));
			font.setWeight(QFont::ExtraBold);
			p.setFont(font); p.setPen(Qt::white);
			p.drawText(bounds.translated(0, -.5 * scale), Qt::AlignCenter, _event.glyph);
		}
		return;
	}
	const auto radius = rect.width() / 4;
	p.setPen(Qt::NoPen);
	p.setBrush(st::windowBgOver);
	p.drawRoundedRect(rect, radius, radius);
	if (_event.type == Event::Type::Hidden) {
		st::menuIconCaptionHide.paintInCenter(p, rect);
		return;
	}
	p.setPen(st::windowSubTextFg);
	p.setFont(st::semiboldFont);
	const auto glyph = (_event.field == u"username"_q
		|| _event.field == u"usernames"_q)
		? u"@"_q
		: (_event.field == u"premium"_q)
		? u"P"_q
		: (_event.field == u"verified"_q)
		? QString::fromUtf8("\xe2\x9c\x93")
		: (_event.field == u"scam"_q || _event.field == u"fake"_q)
		? u"!"_q
		: (_event.field == u"deleted"_q)
		? QString::fromUtf8("\xe2\x80\x93")
		: u"A"_q;
	p.drawText(rect, glyph, style::al_center);
}

void Row::paintEvent(QPaintEvent *e) {
	auto p = Painter(this);
	// Every event is its own block, like the mini app: a card of the window
	// colour on the page's divider background.
	const auto block = _geometry.block;
	{
		auto hq = PainterHighQualityEnabler(p);
		p.setPen(Qt::NoPen);
		p.setBrush(st::windowBg);
		const auto radius = st::giftBoxGiftRadius;
		p.drawRoundedRect(block, radius, radius);
	}
	if (!_card) {
		paintGlyph(p, style::rtlrect(_geometry.art, width()));
	}
	const auto textLeft = _geometry.text.x();
	const auto available = _geometry.text.width();
	auto lines = QStringList();
	lines.push_back(_title);
	for (const auto &line : _meta) {
		lines.push_back(line);
	}
	const auto ownersFrom = lines.size();
	for (auto i = 0; i != int(_owners.size()); ++i) {
		lines.push_back(QString());
	}
	const auto ownersTill = lines.size();
	if (!_time.isEmpty()) {
		lines.push_back(_time);
	}
	const auto count = lines.size();
	const auto titleHeight = TitleFont()->height;
	const auto lineHeight = std::max(LineFont()->height, style::ConvertScale(kUserpicSize));
	const auto textHeight = int(titleHeight + (count - 1) * lineHeight);
	const auto span = _geometry.text.height();
	const auto top = _geometry.text.y();
	const auto gap = (count > 1)
		? float64(span - textHeight) / (count - 1)
		: 0.;
	auto y = float64(top);
	for (auto i = 0; i != count; ++i) {
		const auto rounded = int(base::SafeRound(y));
		if (i == 0) {
			p.setFont(TitleFont()->f);
			p.setPen(st::windowFg);
			p.drawTextLeft(
				textLeft,
				rounded,
				width(),
				TitleFont()->elided(
					(!_shortTitle.isEmpty() && TitleFont()->width(_title) > available)
						? _shortTitle : _title,
					std::max(0, available)));
			y += titleHeight + gap;
			continue;
		}
		if (i >= ownersFrom && i < ownersTill) {
			_owners[i - ownersFrom]->paint(p, textLeft, rounded + (lineHeight - LineFont()->height) / 2, available, width());
		} else {
			p.setFont(LineFont()->f);
			p.setPen(st::windowSubTextFg);
			p.drawTextLeft(
				textLeft,
				rounded,
				width(),
				LineFont()->elided(lines[i], available));
		}
		y += lineHeight + gap;
	}
}

void OpenUrl(const QString &url) {
	UrlClickHandler::Open(url);
}

[[nodiscard]] QString ProfileAppUrl(not_null<PeerData*> peer) {
	return QString::fromLatin1(kAppProfileUrl).arg(
		QString::number(peer->id.value & PeerId::kChatTypeMask));
}

void AddMessage(
		not_null<Ui::VerticalLayout*> container,
		const QString &text) {
	Ui::AddSkip(container, st::defaultVerticalListSkip * 2);
	const auto label = container->add(
		object_ptr<Ui::FlatLabel>(container, text, st::boxDividerLabel),
		st::boxRowPadding);
	label->setTextColorOverride(st::windowSubTextFg->c);
	Ui::AddSkip(container, st::defaultVerticalListSkip * 2);
}

[[nodiscard]] QString HoldAppUrl(const HoldModel &model) {
	const auto payload = (QString::number(model.collectionId)
		+ '|'
		+ model.model).toUtf8();
	return QString::fromLatin1(kAppHoldUrl).arg(
		QString::fromLatin1(payload.toBase64(
			QByteArray::Base64UrlEncoding | QByteArray::OmitTrailingEquals)));
}

// The card that stands where the hidden part of the tape would be, the
// mini app's: a head naming the reason, then the two ways to get the same
// for oneself - the reason's own way first - each with its art, a line
// under the title and a pill button. Each leads straight to its place in
// the mini app.
class HiddenCard final : public Ui::RpWidget {
public:
	HiddenCard(
		QWidget *parent,
		not_null<PeerData*> peer,
		bool wholeHistory,
		const std::vector<HoldModel> &models);

protected:
	int resizeGetHeight(int newWidth) override;
	void paintEvent(QPaintEvent *e) override;
	void mousePressEvent(QMouseEvent *e) override;
	void mouseMoveEvent(QMouseEvent *e) override;
	void leaveEventHook(QEvent *e) override;

private:
	struct Option {
		QString title;
		QString sub;
		QString button;
		QString url;
		QImage art;
		bool star = false;
	};
	struct Layout {
		QRect block;
		QRect icon;
		int textLeft = 0;
		int textWidth = 0;
		int optionsTop = 0;
		int openTop = 0;
		int height = 0;
	};
	static constexpr auto kIconSize = 40;
	static constexpr auto kArtSize = 40;
	static constexpr auto kOptionHeight = 60;
	static constexpr auto kPillHeight = 28;
	static constexpr auto kHeadGap = 14;

	[[nodiscard]] Layout layoutFor(int width);
	[[nodiscard]] int optionAt(QPoint point) const;
	[[nodiscard]] bool openAt(QPoint point) const;

	const not_null<PeerData*> _peer;
	Ui::Text::String _title;
	// A label wraps and measures itself; measuring wrapped text by hand
	// is exactly how the head came to overlap the rows.
	const not_null<Ui::FlatLabel*> _text;
	std::vector<std::unique_ptr<Option>> _options;
	QString _open;
	QImage _star;
	Layout _layout;
	int _hover = -1;

};

HiddenCard::HiddenCard(
	QWidget *parent,
	not_null<PeerData*> peer,
	bool wholeHistory,
	const std::vector<HoldModel> &models)
: RpWidget(parent)
, _peer(peer)
, _text(Ui::CreateChild<Ui::FlatLabel>(
	this,
	wholeHistory
		? Lang::Text(Key::SeeTgHistoryHiddenText)
		: Lang::Text(Key::SeeTgHistoryTransfersHiddenText),
	st::boxDividerLabel))
, _open(Lang::Text(Key::SeeTgHistoryOpenApp)) {
	_title.setText(
		st::semiboldTextStyle,
		wholeHistory
			? Lang::Text(Key::SeeTgHistoryHiddenTitle)
			: Lang::Text(Key::SeeTgHistoryTransfersHiddenTitle));
	_text->setAttribute(Qt::WA_TransparentForMouseEvents);
	_text->show();
	setMouseTracking(true);

	const auto premium = [&] {
		auto option = std::make_unique<Option>();
		option->title = u"see.tg Premium"_q;
		option->sub = Lang::Text(Key::SeeTgHistoryPremiumSub);
		option->button = Lang::Text(Key::SeeTgHistoryMore);
		option->url = QString::fromLatin1(kAppPremiumUrl);
		option->star = true;
		_options.push_back(std::move(option));
	};
	const auto hold = [&] {
		if (models.empty()) {
			return;
		}
		const auto &m = models.front();
		auto option = std::make_unique<Option>();
		option->title = Lang::Text(Key::SeeTgHistoryHoldTitle).replace(
			u"{m}"_q,
			m.model);
		option->sub = wholeHistory
			? Lang::Text(Key::SeeTgHistoryHoldSubBase)
			: Lang::Text(Key::SeeTgHistoryHoldSub);
		option->button = Lang::Text(Key::SeeTgHistoryBuy);
		option->url = HoldAppUrl(m);
		const auto raw = option.get();
		Visuals::Image(
			Visuals::ModelImageUrl(m.collectionId, m.model),
			crl::guard(this, [=](QImage image) {
				raw->art = std::move(image);
				update();
			}));
		_options.push_back(std::move(option));
	};
	if (wholeHistory) {
		premium();
		hold();
	} else {
		hold();
		premium();
	}
}

HiddenCard::Layout HiddenCard::layoutFor(int width) {
	auto result = Layout();
	result.block = QRect(
		kBlockMarginX,
		kBlockMarginY,
		width - 2 * kBlockMarginX,
		0);
	result.icon = QRect(
		result.block.x() + kBlockPadding,
		result.block.y() + kBlockPadding,
		kIconSize,
		kIconSize);
	result.textLeft = result.icon.x() + kIconSize + kBlockPadding;
	result.textWidth = result.block.x() + result.block.width()
		- kBlockPadding
		- result.textLeft;
	_text->resizeToWidth(result.textWidth);
	_text->moveToLeft(
		result.textLeft,
		result.icon.y() + st::semiboldFont->height + 2,
		width);
	const auto headBottom = std::max(
		result.icon.y() + kIconSize,
		_text->y() + _text->height());
	result.optionsTop = headBottom + kHeadGap;
	result.openTop = result.optionsTop
		+ int(_options.size()) * kOptionHeight
		+ kBlockPadding;
	result.height = result.openTop
		+ st::semiboldFont->height
		+ kBlockPadding + 2
		+ kBlockMarginY;
	return result;
}

int HiddenCard::resizeGetHeight(int newWidth) {
	_layout = layoutFor(newWidth);
	return _layout.height;
}

int HiddenCard::optionAt(QPoint point) const {
	const auto &layout = _layout;
	for (auto i = 0; i != int(_options.size()); ++i) {
		const auto rect = QRect(
			layout.block.x(),
			layout.optionsTop + i * kOptionHeight,
			layout.block.width(),
			kOptionHeight);
		if (rect.contains(point)) {
			return i;
		}
	}
	return -1;
}

bool HiddenCard::openAt(QPoint point) const {
	const auto &layout = _layout;
	return QRect(
		layout.block.x(),
		layout.openTop - kBlockPadding / 2,
		layout.block.width(),
		st::semiboldFont->height + kBlockPadding).contains(point);
}

void HiddenCard::mouseMoveEvent(QMouseEvent *e) {
	const auto option = optionAt(e->pos());
	const auto open = openAt(e->pos());
	const auto hover = open ? int(_options.size()) : option;
	if (_hover != hover) {
		_hover = hover;
		setCursor((hover >= 0) ? style::cur_pointer : style::cur_default);
		update();
	}
}

void HiddenCard::leaveEventHook(QEvent *e) {
	if (_hover != -1) {
		_hover = -1;
		setCursor(style::cur_default);
		update();
	}
}

void HiddenCard::mousePressEvent(QMouseEvent *e) {
	if (e->button() != Qt::LeftButton) {
		return;
	}
	if (openAt(e->pos())) {
		OpenUrl(ProfileAppUrl(_peer));
	} else if (const auto i = optionAt(e->pos()); i >= 0) {
		OpenUrl(_options[i]->url);
	}
}

void HiddenCard::paintEvent(QPaintEvent *e) {
	auto p = Painter(this);
	const auto &layout = _layout;
	const auto block = QRect(
		layout.block.x(),
		layout.block.y(),
		layout.block.width(),
		height() - 2 * kBlockMarginY);
	{
		auto hq = PainterHighQualityEnabler(p);
		p.setPen(Qt::NoPen);
		p.setBrush(st::windowBg);
		const auto radius = st::giftBoxGiftRadius;
		p.drawRoundedRect(block, radius, radius);
	}

	// The head: the eye in a circle, the title, the reason.
	{
		auto hq = PainterHighQualityEnabler(p);
		p.setPen(Qt::NoPen);
		p.setBrush(st::windowBgOver);
		p.drawEllipse(layout.icon);
		st::menuIconCaptionHide.paintInCenter(p, layout.icon);
	}
	p.setPen(st::windowFg);
	_title.drawLeftElided(
		p,
		layout.textLeft,
		layout.icon.y(),
		layout.textWidth,
		width());

	// The options: art, title, a line under it, a pill on the right.
	for (auto i = 0; i != int(_options.size()); ++i) {
		const auto &option = *_options[i];
		const auto top = layout.optionsTop + i * kOptionHeight;
		if (_hover == i) {
			p.fillRect(
				QRect(block.x(), top, block.width(), kOptionHeight),
				st::windowBgOver);
		}
		// A hairline above each row, like the rows of a settings list.
		p.fillRect(
			QRect(
				block.x() + kBlockPadding,
				top,
				block.width() - 2 * kBlockPadding,
				st::lineWidth),
			st::boxDividerBg);
		const auto artRect = QRect(
			block.x() + kBlockPadding,
			top + (kOptionHeight - kArtSize) / 2,
			kArtSize,
			kArtSize);
		{
			auto hq = PainterHighQualityEnabler(p);
			p.setPen(Qt::NoPen);
			p.setBrush(option.star ? st::windowBgActive : st::windowBgOver);
			p.drawEllipse(artRect);
			if (option.star) {
				if (_star.isNull()) {
					_star = Ui::CreditsWhiteDoubledIcon(kArtSize * 3 / 5, 1.);
				}
				const auto size = _star.size() / _star.devicePixelRatio();
				p.drawImage(
					artRect.x() + (kArtSize - size.width()) / 2,
					artRect.y() + (kArtSize - size.height()) / 2,
					_star);
			} else if (!option.art.isNull()) {
				p.drawImage(artRect.marginsRemoved({ 3, 3, 3, 3 }), option.art);
			}
		}
		// The pill.
		const auto pillWidth = st::semiboldFont->width(option.button)
			+ 2 * st::semiboldFont->spacew * 3;
		const auto pill = QRect(
			block.x() + block.width() - kBlockPadding - pillWidth,
			top + (kOptionHeight - kPillHeight) / 2,
			pillWidth,
			kPillHeight);
		{
			auto hq = PainterHighQualityEnabler(p);
			p.setPen(Qt::NoPen);
			p.setBrush(st::lightButtonBg);
			p.drawRoundedRect(pill, kPillHeight / 2., kPillHeight / 2.);
			p.setPen(st::lightButtonFg);
			p.setFont(st::semiboldFont);
			p.drawText(pill, option.button, style::al_center);
		}
		const auto left = artRect.x() + kArtSize + kBlockPadding;
		const auto right = pill.x() - kBlockPadding;
		const auto lineTop = top
			+ (kOptionHeight - st::semiboldFont->height - st::normalFont->height) / 2;
		p.setFont(st::semiboldFont);
		p.setPen(st::windowFg);
		p.drawTextLeft(
			left,
			lineTop,
			width(),
			st::semiboldFont->elided(option.title, right - left));
		p.setFont(st::normalFont);
		p.setPen(st::windowSubTextFg);
		p.drawTextLeft(
			left,
			lineTop + st::semiboldFont->height,
			width(),
			st::normalFont->elided(option.sub, right - left));
	}

	// «Открыть в see.tg», a link line at the bottom.
	p.setFont(st::semiboldFont);
	p.setPen((_hover == int(_options.size()))
		? st::windowActiveTextFg
		: st::windowActiveTextFg);
	p.drawText(
		QRect(block.x(), layout.openTop, block.width(), st::semiboldFont->height),
		_open,
		style::al_center);
}

void AddHiddenCard(
		not_null<Ui::VerticalLayout*> container,
		not_null<PeerData*> peer,
		bool wholeHistory,
		const std::vector<HoldModel> &models) {
	container->add(object_ptr<HiddenCard>(
		container,
		peer,
		wholeHistory,
		models));
}

// The event's own sheet, like the mini app's: the card large, then who
// gave what to whom and when. This is the one place a participant is looked
// up - by the user's own rule: at once when looking up automatically is on,
// by «Узнать кто» otherwise.
void AddOwnerRow(
		not_null<Ui::TableLayout*> table,
		std::shared_ptr<ChatHelpers::Show> show,
		rpl::producer<QString> label,
		const Owner &owner) {
	if (!owner.known()) {
		if (!owner.name.isEmpty()) {
			Ui::AddTableRow(
				table,
				std::move(label),
				rpl::single(TextWithEntities{ owner.name }));
		}
		return;
	}
	auto unknown = Peers::MakeUnknownSenderValue(table, show, owner.peerId);
	Ui::AddTableRow(
		table,
		std::move(label),
		unknown
			? std::move(unknown)
			: Ui::MakePeerTableValue(table, show, owner.peerId),
		st::giveawayGiftCodePeerMargin);
}

void AddEventTable(
	not_null<Ui::VerticalLayout*> content,
	std::shared_ptr<ChatHelpers::Show> show,
	const Event &event,
	bool withGiftRow);

void EventBox(
		not_null<Ui::GenericBox*> box,
		not_null<Window::SessionController*> controller,
		Event event) {
	const auto text = TextFor(event);
	box->setTitle(rpl::single(text.title));
	box->setWidth(st::boxWideWidth);

	const auto content = box->verticalLayout();
	Ui::AddSkip(content);
	const auto cardWrap = content->add(
		object_ptr<Ui::FixedHeightWidget>(content, kBoxCardSize));
	const auto card = Ui::CreateChild<Card>(cardWrap, CardFor(event));
	card->resize(kBoxCardSize, kBoxCardSize);
	cardWrap->widthValue() | rpl::on_next([=](int width) {
		card->move((width - kBoxCardSize) / 2, 0);
	}, card->lifetime());
	card->show();
	Ui::AddSkip(content);
	Ui::AddSkip(content);

	const auto show = controller->uiShow();
	AddEventTable(content, show, event, true);
	Ui::AddSkip(content);

	if (event.type == Event::Type::Gift && !event.slug.isEmpty()) {
		const auto slug = event.slug + '-' + QString::number(event.num);
		box->addButton(Lang::Value(Key::SeeTgHistoryOpenGift), [=] {
			Core::ResolveAndShowUniqueGift(show, slug);
		});
	}
	box->addButton(tr::lng_close(), [=] { box->closeBox(); });
}

// The event's own table under whichever cover the sheet got.
void AddEventTable(
		not_null<Ui::VerticalLayout*> content,
		std::shared_ptr<ChatHelpers::Show> show,
		const Event &event,
		bool withGiftRow) {
	const auto table = content->add(
		object_ptr<Ui::TableLayout>(content, st::giveawayGiftCodeTable),
		st::giveawayGiftCodeTableMargin);
	if (withGiftRow && event.type == Event::Type::Gift) {
		Ui::AddTableRow(
			table,
			Lang::Value(Key::SeeTgHistoryGiftLabel),
			rpl::single(TextWithEntities{
				event.title + " #" + ::Lang::FormatCountDecimal(event.num) }));
	} else if (withGiftRow && event.savedGiftId) {
		const auto name = GiftName(event.savedGiftId);
		auto text = TextWithEntities{ name.isEmpty()
			? Lang::Text(Key::SeeTgHistorySavedGift)
			: name };
		if (event.stars) {
			text.append(", ").append(Ui::MakeCreditsIconEntity()).append(
				' ' + QString::number(event.stars));
		}
		Ui::AddTableRow(
			table,
			Lang::Value(Key::SeeTgHistoryGiftLabel),
			rpl::single(text),
			Ui::MakeCreditsIconContext(
				table->st().defaultValue.style.font->height,
				1));
	}
	const auto upgrade = (event.type == Event::Type::Gift
		&& event.action == u"new"_q);
	if (!upgrade && event.type != Event::Type::SavedVisibility) {
		AddOwnerRow(table, show, Lang::Value(Key::SeeTgHistoryFromLabel), event.from);
		AddOwnerRow(table, show, Lang::Value(Key::SeeTgHistoryToLabel), event.to);
	}
	if (event.type == Event::Type::SavedVisibility && event.change == u"gone"_q) {
		Ui::AddTableRow(
			table,
			Lang::Value(Key::SeeTgHistoryGiftLabel),
			rpl::single(TextWithEntities{ GoneHint(event) }));
	}
	for (const auto &detail : event.eventDetails) {
		const auto split = detail.indexOf(u": "_q);
		Ui::AddTableRow(table, rpl::single(split < 0 ? Lang::Text(Key::GiftHistoryPriceLabel) : detail.left(split)),
			rpl::single(TextWithEntities{ split < 0 ? detail : detail.mid(split + 2) }));
	}
	if (!event.message.isEmpty()) {
		Ui::AddTableRow(
			table,
			tr::lng_gift_link_label_reason(),
			rpl::single(TextWithEntities{ event.message }));
	}
	if (event.time) {
		Ui::AddTableRow(
			table,
			Lang::Value(Key::SeeTgHistoryDateLabel),
			rpl::single(TextWithEntities{
				langDateTime(base::unixtime::parse(event.time)) }));
	}
}

// The close and menu buttons the client puts over a gift cover - its own
// helper for them is not exported, so this is the same thing, minus the
// craft button no event needs.
void AddCloseAndMenu(
		not_null<Ui::GenericBox*> box,
		Fn<void(not_null<Ui::PopupMenu*>)> fillMenu) {
	const auto close = Ui::CreateChild<Ui::IconButton>(
		box,
		st::uniqueCloseButton);
	const auto menu = Ui::CreateChild<Ui::IconButton>(
		box,
		st::uniqueMenuButton);
	close->show();
	menu->show();
	box->widthValue() | rpl::on_next([=](int width) {
		close->moveToRight(0, 0, width);
		close->raise();
		menu->moveToRight(close->width(), 0, width);
		menu->raise();
	}, close->lifetime());
	close->setClickedCallback([=] { box->closeBox(); });
	const auto popup = box->lifetime().make_state<
		base::unique_qptr<Ui::PopupMenu>>();
	menu->setClickedCallback([=] {
		*popup = base::make_unique_q<Ui::PopupMenu>(
			box,
			st::popupMenuWithIcons);
		fillMenu(popup->get());
		(*popup)->popup(QCursor::pos());
	});
}

// The sheet the client shows for a collectible, with the event's table in
// place of the gift's: the same cover - backdrop, pattern, the model alive -
// and the event's title on the pill above the name.
void NativeEventBox(
		not_null<Ui::GenericBox*> box,
		not_null<Window::SessionController*> controller,
		not_null<PeerData*> peer,
		Event event,
		Data::UniqueGift unique) {
	const auto text = TextFor(event);
	const auto show = controller->uiShow();
	box->setStyle(st::giveawayGiftCodeBox);
	box->setWidth(st::boxWideWidth);
	box->setNoContentMargin(true);

	const auto content = box->verticalLayout();
	Ui::AddUniqueGiftCover(
		content,
		rpl::single(Ui::UniqueGiftCover{ unique }),
		{
			.pretitle = rpl::single(text.title),
			.numberText = (unique.number > 0)
				? rpl::single(u"#"_q + ::Lang::FormatCountDecimal(unique.number))
				: rpl::producer<QString>(),
		});
	Ui::AddSkip(content);
	Ui::AddSkip(content);
	AddEventTable(content, show, event, false);
	Ui::AddSkip(content);

	const auto slug = event.slug + '-' + QString::number(event.num);
	AddCloseAndMenu(box, [=](not_null<Ui::PopupMenu*> menu) {
		menu->addAction(
			Lang::Text(Key::SeeTgHistoryOpenGift),
			[=] { Core::ResolveAndShowUniqueGift(show, slug); },
			&st::menuIconGiftPremium);
		menu->addAction(
			Lang::Text(Key::SeeTgHistoryOpenApp),
			[=] { OpenUrl(ProfileAppUrl(peer)); },
			&st::menuIconLinks);
	});
	box->addButton(tr::lng_box_ok(), [=] { box->closeBox(); });
}

// A small rounded pill with the event's title, the way the collectible's
// cover carries it above the name.
void AddTitlePill(
		not_null<Ui::VerticalLayout*> content,
		const QString &title) {
	const auto wrap = content->add(
		object_ptr<Ui::FixedHeightWidget>(
			content,
			st::semiboldFont->height + 2 * st::normalFont->spacew));
	wrap->paintRequest() | rpl::on_next([=] {
		auto p = Painter(wrap);
		auto hq = PainterHighQualityEnabler(p);
		const auto font = st::semiboldFont;
		const auto width = font->width(title) + 4 * font->spacew;
		const auto rect = QRect(
			(wrap->width() - width) / 2,
			0,
			width,
			wrap->height());
		p.setPen(Qt::NoPen);
		p.setBrush(st::windowBgOver);
		p.drawRoundedRect(rect, rect.height() / 2., rect.height() / 2.);
		p.setPen(st::windowFg);
		p.setFont(font);
		p.drawText(rect, title, style::al_center);
	}, wrap->lifetime());
}

// The collection's sticker, alive, exactly as the client's own gift sheet
// shows it. The animation comes from the shop catalog's document while the
// collection is sold, or from changes.tg's copy of the same .tgs when it no
// longer is.
struct StickerSource {
	DocumentData *document = nullptr;
	QByteArray tgs;
};

void AddStickerCover(
		not_null<Ui::VerticalLayout*> content,
		not_null<Main::Session*> session,
		StickerSource source) {
	struct State {
		std::shared_ptr<Data::DocumentMedia> media;
		std::unique_ptr<Lottie::SinglePlayer> lottie;
		rpl::lifetime downloadLifetime;
	};
	const auto space = content->add(object_ptr<Ui::FixedHeightWidget>(
		content,
		st::creditsHistoryEntryStarGiftSpace));
	const auto icon = Ui::CreateChild<Ui::RpWidget>(space);
	icon->resize(
		st::creditsHistoryEntryStarGiftSize,
		st::creditsHistoryEntryStarGiftSize);
	const auto state = icon->lifetime().make_state<State>();
	const auto attach = [=] {
		state->lottie->updates() | rpl::on_next([=] {
			icon->update();
		}, icon->lifetime());
	};
	if (const auto document = source.document) {
		const auto origin = document->stickerOrGifOrigin();
		state->media = document->createMediaView();
		state->media->thumbnailWanted(origin);
		state->media->automaticLoad(origin, nullptr);
		rpl::single() | rpl::then(
			session->downloaderTaskFinished()
		) | rpl::filter([=] {
			return state->media->loaded();
		}) | rpl::on_next([=] {
			state->lottie = ChatHelpers::LottiePlayerFromDocument(
				state->media.get(),
				ChatHelpers::StickerLottieSize::MessageHistory,
				icon->size(),
				Lottie::Quality::High);
			attach();
			state->downloadLifetime.destroy();
		}, state->downloadLifetime);
	} else if (!source.tgs.isEmpty()) {
		const auto factor = style::DevicePixelRatio();
		state->lottie = std::make_unique<Lottie::SinglePlayer>(
			Lottie::ReadContent(source.tgs, QString()),
			Lottie::FrameRequest{ .box = icon->size() * factor },
			Lottie::Quality::High);
		attach();
	}
	icon->paintRequest() | rpl::on_next([=] {
		auto p = Painter(icon);
		const auto &lottie = state->lottie;
		const auto factor = style::DevicePixelRatio();
		const auto request = Lottie::FrameRequest{
			.box = icon->size() * factor,
		};
		const auto frame = (lottie && lottie->ready())
			? lottie->frameInfo(request)
			: Lottie::Animation::FrameInfo();
		if (!frame.image.isNull()) {
			p.drawImage(
				QRect(QPoint(), frame.image.size() / factor),
				frame.image);
			if (lottie->frameIndex() < lottie->framesCount() - 1) {
				lottie->markFrameShown();
			}
		}
	}, icon->lifetime());
	space->widthValue() | rpl::on_next([=](int width) {
		icon->move(
			(width - icon->width()) / 2,
			st::creditsHistoryEntryStarGiftSkip);
	}, icon->lifetime());
	icon->show();
}

// The sheet the client shows for a regular gift, with the event's table:
// the collection's sticker large on top, the event's title on a pill, the
// gift's name and price under it, then who and when.
void SavedEventBox(
		not_null<Ui::GenericBox*> box,
		not_null<Window::SessionController*> controller,
		not_null<PeerData*> peer,
		Event event,
		StickerSource sticker) {
	const auto text = TextFor(event);
	const auto show = controller->uiShow();
	const auto session = &controller->session();
	box->setStyle(st::giveawayGiftCodeBox);
	box->setWidth(st::boxWideWidth);
	box->setNoContentMargin(true);

	const auto content = box->verticalLayout();
	Ui::AddSkip(content);
	AddStickerCover(content, session, sticker);
	AddTitlePill(content, text.title);
	Ui::AddSkip(content);
	const auto name = GiftName(event.savedGiftId);
	content->add(
		object_ptr<Ui::FlatLabel>(
			content,
			rpl::single(name.isEmpty()
				? Lang::Text(Key::SeeTgHistorySavedGift)
				: name),
			st::creditsBoxAboutTitle),
		style::al_top);
	if (event.stars) {
		const auto &st = st::creditsBoxAbout;
		auto price = TextWithEntities();
		price.append(Ui::MakeCreditsIconEntity()).append(
			' ' + QString::number(event.stars));
		content->add(
			object_ptr<Ui::FlatLabel>(
				content,
				rpl::single(price),
				st,
				st::defaultPopupMenu,
				Ui::MakeCreditsIconContext(st.style.font->height, 1)),
			style::al_top);
	}
	Ui::AddSkip(content);
	Ui::AddSkip(content);
	AddEventTable(content, show, event, false);
	Ui::AddSkip(content);
	box->addButton(tr::lng_box_ok(), [=] { box->closeBox(); });
}

void ShowEventBox(
		not_null<Window::SessionController*> controller,
		not_null<PeerData*> peer,
		Event event,
		std::optional<StickerSource> saved) {
	const auto session = &controller->session();
	// A collectible is asked from Telegram - one request, when the sheet
	// opens - so the sheet can be the client's own cover with the model
	// alive. Anything else, and any failure, gets the drawn card.
	const auto open = [=] {
		if (event.type != Event::Type::Gift || event.slug.isEmpty()) {
			if (saved) {
				controller->show(Box(
					SavedEventBox,
					controller,
					peer,
					event,
					*saved));
			} else {
				controller->show(Box(EventBox, controller, event));
			}
			return;
		}
		const auto slug = event.slug + '-' + QString::number(event.num);
		session->api().request(
			MTPpayments_GetUniqueStarGift(MTP_string(slug))
		).done(crl::guard(controller, [=](
				const MTPpayments_UniqueStarGift &result) {
			const auto &data = result.data();
			session->data().processUsers(data.vusers());
			const auto gift = ::Api::FromTL(session, data.vgift());
			if (gift && gift->unique) {
				controller->show(Box(
					NativeEventBox,
					controller,
					peer,
					event,
					*gift->unique));
			} else {
				controller->show(Box(EventBox, controller, event));
			}
		})).fail(crl::guard(controller, [=] {
			controller->show(Box(EventBox, controller, event));
		})).send();
	};
	if (!Current().resolveAutomatically) {
		open();
		return;
	}
	// Both participants looked up before the sheet opens, one after the
	// other; a failure just leaves that one with its «Узнать кто».
	auto pending = std::make_shared<std::vector<Owner>>();
	for (const auto &owner : { event.from, event.to }) {
		if (owner.known() && Peers::Unknown(session, owner.peerId)) {
			pending->push_back(owner);
		}
	}
	if (pending->empty()) {
		open();
		return;
	}
	auto step = std::make_shared<Fn<void()>>();
	*step = [=] {
		if (pending->empty()) {
			open();
			return;
		}
		const auto owner = pending->back();
		pending->pop_back();
		Peers::Resolve(session, owner, crl::guard(controller, [=](PeerData*) {
			(*step)();
		}));
	};
	(*step)();
}

// The page: the tab strip on top, then the tape of the chosen tab. Rebuilt
// from scratch on a tab switch; grows by a page when scrolled near its end.
class Inner final : public Ui::RpWidget {
public:
	Inner(
		QWidget *parent,
		not_null<Window::SessionController*> controller,
		not_null<PeerData*> peer);

protected:
	int resizeGetHeight(int newWidth) override;
	void visibleTopBottomUpdated(int visibleTop, int visibleBottom) override;
	void paintEvent(QPaintEvent *e) override;

private:
	enum class Status {
		Loading,
		Ready,
		Empty,
		Hidden,
		DailyLimit,
		ProfileLimit,
		Error,
	};

	void setTab(Tab tab);
	void reload();
	void loadMore();
	void request(const QString &after);
	void apply(PageData &&page, bool first);
	void fail(const Api::Error &error);
	void rebuild();
	void appendRows(const std::vector<Event> &events);
	void loadHoldModels();
	void openEvent(const Event &event);
	void ensureCatalog(Fn<void()> done);

public:
	void fillMenu(const Ui::Menu::MenuCallback &addAction);

private:

	const not_null<Window::SessionController*> _controller;
	const std::unique_ptr<::Api::PremiumGiftCodeOptions> _catalog;
	const not_null<PeerData*> _peer;
	const QString _seeId;
	const not_null<Ui::SubTabs*> _tabs;
	const not_null<Ui::VerticalLayout*> _content;

	Tab _tab = Tab::Nft;
	Filters _filters;
	Status _status = Status::Loading;
	bool _transfersHidden = false;
	std::vector<Event> _events;
	QString _cursor;
	bool _hasNext = false;
	bool _loading = false;
	int _generation = 0;
	std::vector<HoldModel> _holdModels;
	bool _holdRequested = false;
	Ui::VerticalLayout *_rows = nullptr;

};

Inner::Inner(
	QWidget *parent,
	not_null<Window::SessionController*> controller,
	not_null<PeerData*> peer)
: RpWidget(parent)
, _controller(controller)
, _catalog(std::make_unique<::Api::PremiumGiftCodeOptions>(
	controller->session().user()))
, _peer(peer)
, _seeId(SeeId(peer->id))
, _tabs(Ui::CreateChild<Ui::SubTabs>(
	this,
	st::collectionSubTabs,
	Ui::SubTabsOptions{ .selected = u"nft"_q, .centered = true },
	std::vector<Ui::SubTabsTab>{
		{ u"nft"_q, TextWithEntities{ Lang::Text(Key::SeeTgHistoryTabNft) } },
		{ u"saved"_q, TextWithEntities{ Lang::Text(Key::SeeTgKindRegular) } },
		{ u"info"_q, TextWithEntities{ Lang::Text(Key::SeeTgHistoryTabInfo) } },
	}))
, _content(Ui::CreateChild<Ui::VerticalLayout>(this)) {
	_tabs->show();
	_tabs->activated() | rpl::on_next([=](const QString &id) {
		_tabs->setActiveTab(id);
		setTab((id == u"saved"_q)
			? Tab::Saved
			: (id == u"info"_q)
			? Tab::Info
			: Tab::Nft);
	}, lifetime());
	_content->heightValue() | rpl::on_next([=] {
		resizeToWidth(width());
	}, lifetime());
	reload();
}

int Inner::resizeGetHeight(int newWidth) {
	// The same spacing as the client's own gifts page: the tab strip under
	// the top padding, the tape a bottom padding below it.
	const auto &padding = st::giftBoxPadding;
	auto top = padding.top();
	_tabs->resizeToWidth(newWidth);
	_tabs->move(0, top);
	top += _tabs->height() + padding.bottom();
	_content->resizeToWidth(newWidth);
	_content->move(0, top);
	return top + _content->height() + padding.bottom();
}

void Inner::paintEvent(QPaintEvent *e) {
	QPainter(this).fillRect(e->rect(), st::boxDividerBg);
}

void Inner::ensureCatalog(Fn<void()> done) {
	if (!_catalog->starGifts().empty()) {
		done();
		return;
	}
	_catalog->requestStarGifts(
	) | rpl::on_next_error_done([] {
	}, [=](const QString &) {
		done();
	}, [=] {
		done();
	}, lifetime());
}

// A regular gift's sheet wants the collection's sticker, which only the
// shop catalog has; it is fetched once, the first time such an event opens.
void Inner::openEvent(const Event &event) {
	const auto controller = _controller;
	const auto peer = _peer;
	if (event.type == Event::Type::Gift) {
		ShowEventBox(controller, peer, event, std::nullopt);
		return;
	}
	const auto session = &_controller->session();
	ensureCatalog(crl::guard(this, [=] {
		EnsureNames(session, crl::guard(this, [=] {
			const auto &gifts = _catalog->starGifts();
			const auto i = ranges::find(
				gifts,
				event.savedGiftId,
				&Data::StarGift::id);
			if (i != end(gifts) && i->document) {
				ShowEventBox(controller, peer, event, StickerSource{
					.document = i->document,
				});
				return;
			}
			Visuals::Fetch(
				Visuals::OriginalAnimationUrl(event.savedGiftId),
				crl::guard(controller, [=](QByteArray tgs) {
					ShowEventBox(
						controller,
						peer,
						event,
						tgs.isEmpty()
							? std::nullopt
							: std::optional<StickerSource>(
								StickerSource{ .tgs = tgs }));
				}));
		}));
	}));
}

// The ⋮ menu: the sort, then what the current tab can narrow - the mini
// app's history filter sheet, as menu items.
void Inner::fillMenu(const Ui::Menu::MenuCallback &addAction) {
	// The client's own way to show a switch in a menu: a check mark on the
	// active item, nothing on the others.
	const auto check = [](bool active) {
		return active ? &st::mediaPlayerMenuCheck : nullptr;
	};
	const auto toggle = [&](const QString &text, bool Filters::*field) {
		addAction(text, crl::guard(this, [=] {
			_filters.*field = !(_filters.*field);
			reload();
		}), check(_filters.*field));
	};
	addAction(Ui::Menu::MenuCallback::Args{
		.text = Lang::Text(Key::SeeTgSort),
		.icon = &st::menuIconReorder,
		.fillSubmenu = [=](not_null<Ui::PopupMenu*> menu) {
			const auto add = [&](const QString &text, bool newest) {
				menu->addAction(text, crl::guard(this, [=] {
					if (_filters.newestFirst != newest) {
						_filters.newestFirst = newest;
						reload();
					}
				}), check(_filters.newestFirst == newest));
			};
			add(Lang::Text(Key::SeeTgHistoryNewestFirst), true);
			add(Lang::Text(Key::SeeTgHistoryOldestFirst), false);
		},
	});
	addAction({ .isSeparator = true });
	switch (_tab) {
	case Tab::Nft:
		toggle(Lang::Text(Key::SeeTgHistorySent), &Filters::sent);
		toggle(Lang::Text(Key::SeeTgHistoryReceived), &Filters::received);
		break;
	case Tab::Saved:
		toggle(Lang::Text(Key::SeeTgHistorySent), &Filters::savedSent);
		toggle(Lang::Text(Key::SeeTgHistoryReceived), &Filters::savedReceived);
		toggle(Lang::Text(Key::SeeTgHistoryGone), &Filters::savedVisibility);
		break;
	case Tab::Info:
		for (const auto &field : kProfileFields) {
			const auto shown = !_filters.hiddenFields.contains(field);
			addAction(FieldLabel(field), crl::guard(this, [=] {
				if (!_filters.hiddenFields.remove(field)) {
					_filters.hiddenFields.emplace(field);
				}
				reload();
			}), check(shown));
		}
		break;
	}
}

void Inner::setTab(Tab tab) {
	if (_tab == tab) {
		return;
	}
	_tab = tab;
	reload();
}

void Inner::visibleTopBottomUpdated(int visibleTop, int visibleBottom) {
	if (visibleBottom >= height() - kLoadMoreThreshold) {
		loadMore();
	}
}

void Inner::reload() {
	++_generation;
	_loading = false;
	_events.clear();
	_cursor = QString();
	_hasNext = false;
	_transfersHidden = false;
	_status = Status::Loading;
	rebuild();
	request(QString());
}

void Inner::loadMore() {
	if (_status != Status::Ready || !_hasNext || _loading) {
		return;
	}
	request(_cursor);
}

void Inner::request(const QString &after) {
	_loading = true;
	const auto generation = _generation;
	const auto first = after.isEmpty();
	const auto document = QString::fromLatin1(
		(_tab == Tab::Saved) ? kDocSaved : kDocNft);
	Api::Query(
		&_controller->session(),
		document,
		VariablesFor(_tab, _seeId, after, _filters),
		crl::guard(this, [=](const QJsonObject &data) {
			if (generation != _generation) {
				return;
			}
			_loading = false;
			apply(ParsePage(data.value(u"profileHistory"_q).toObject()), first);
		}),
		crl::guard(this, [=](const Api::Error &error) {
			if (generation != _generation) {
				return;
			}
			_loading = false;
			fail(error);
		}));
}

void Inner::apply(PageData &&page, bool first) {
	if (page.hiddenByOwner) {
		_status = Status::Hidden;
		_hasNext = false;
		rebuild();
		loadHoldModels();
		return;
	}
	_transfersHidden = page.transfersHidden;
	_cursor = page.cursor;
	_hasNext = page.hasNext && !page.items.empty();
	if (first) {
		_events = std::move(page.items);
		_status = _events.empty() ? Status::Empty : Status::Ready;
		rebuild();
		if (_transfersHidden) {
			loadHoldModels();
		}
	} else {
		appendRows(page.items);
		_events.insert(
			end(_events),
			std::make_move_iterator(begin(page.items)),
			std::make_move_iterator(end(page.items)));
	}
}

void Inner::fail(const Api::Error &error) {
	const auto message = error.message.toLower();
	_status = (message == u"daily limit"_q)
		? Status::DailyLimit
		: (message == u"history limit"_q)
		? Status::ProfileLimit
		: Status::Error;
	_hasNext = false;
	rebuild();
}

void Inner::loadHoldModels() {
	if (_holdRequested) {
		rebuild();
		return;
	}
	_holdRequested = true;
	Api::Query(
		&_controller->session(),
		QString::fromLatin1(kDocHold),
		QJsonObject(),
		crl::guard(this, [=](const QJsonObject &data) {
			const auto status = data.value(u"holdHide"_q).toObject();
			_holdModels.clear();
			if (status.value(u"available"_q).toBool()) {
				for (const auto &item : status.value(u"models"_q).toArray()) {
					const auto o = item.toObject();
					_holdModels.push_back({
						.slug = o.value(u"slug"_q).toString(),
						.title = o.value(u"title"_q).toString(),
						.model = o.value(u"model"_q).toString(),
						.collectionId = ToId(o.value(u"collectionId"_q)),
					});
				}
			}
			rebuild();
		}),
		crl::guard(this, [=](const Api::Error &) {
			rebuild();
		}));
}

void Inner::rebuild() {
	_rows = nullptr;
	while (_content->count() > 0) {
		delete _content->widgetAt(0);
	}
	switch (_status) {
	case Status::Loading:
		AddMessage(_content, Lang::Text(Key::SeeTgLoading));
		return;
	case Status::Hidden:
		AddHiddenCard(_content, _peer, true, _holdModels);
		return;
	case Status::DailyLimit:
		AddMessage(_content, Lang::Text(Key::SeeTgHistoryLimitDay));
		return;
	case Status::ProfileLimit:
		AddMessage(_content, Lang::Text(Key::SeeTgHistoryLimitProfile));
		return;
	case Status::Error:
		AddMessage(_content, Lang::Text(Key::SeeTgHistoryError));
		return;
	case Status::Empty:
	case Status::Ready:
		break;
	}
	if (_tab == Tab::Nft && _transfersHidden) {
		AddHiddenCard(_content, _peer, false, _holdModels);
	}
	if (_status == Status::Empty) {
		// An empty tape under hidden transfers is the card itself, not
		// «no history».
		if (!(_tab == Tab::Nft && _transfersHidden)) {
			AddMessage(_content, Lang::Text(Key::SeeTgHistoryEmpty));
		}
		return;
	}
	_rows = _content->add(object_ptr<Ui::VerticalLayout>(_content));
	appendRows(_events);
}

void Inner::appendRows(const std::vector<Event> &events) {
	if (!_rows) {
		return;
	}
	auto usernames = base::flat_map<PeerId, QString>();
	for (const auto source : std::array<const std::vector<Event>*, 2>{ &_events, &events }) {
		for (const auto &event : *source) {
			for (const auto &owner : { event.from, event.to }) {
				if (owner.known() && !owner.username.isEmpty()) {
					usernames.emplace(owner.peerId, owner.username);
				}
			}
		}
	}
	for (auto event : events) {
		for (const auto owner : { &event.from, &event.to }) {
			if (owner->username.isEmpty()) {
				if (const auto i = usernames.find(owner->peerId); i != end(usernames)) {
					owner->username = i->second;
				}
			}
		}
		const auto row = _rows->add(object_ptr<Row>(_rows, &_controller->session(), event));
		if (HasCard(event)) {
			row->setClickedCallback([=] {
				openEvent(event);
			});
		}
	}
	_rows->resizeToWidth(_content->width());
}

} // namespace

object_ptr<Ui::RpWidget> GiftEventRow(
		QWidget *parent, not_null<Window::SessionController*> controller,
		const QJsonObject &item, QString title, QStringList details) {
	auto event = ParseEvent(item);
	event.compact = true;
	event.eventTitle = std::move(title);
	event.eventDetails = std::move(details);
	event.market = item[u"saleAction"_q].toObject()[u"market"_q].toString();
	const auto kind = item[u"giftAction"_q].toObject()[u"action"_q].toString();
	event.glyph = !event.market.isEmpty() ? u"$"_q : kind == u"listing" ? u"+"_q
		: kind == u"delisting" ? u"−"_q : kind == u"price" ? u"$"_q
		: kind == u"moved" ? u"⇄"_q : kind == u"mint" ? u"#"_q : u"→"_q;
	if (!item[u"hidden"_q].toBool()) {
		const auto action = item[u"giftAction"_q].toObject();
		for (const auto &entry : { std::pair{ &event.from, u"from"_q }, std::pair{ &event.to, u"to"_q } }) {
			const auto owner = action[entry.second].toObject();
			if (entry.first->name.isEmpty()) entry.first->name = owner[u"address"_q].toString();
		}
	}
	auto result = object_ptr<Row>(parent, &controller->session(), event);
	result->setPointerCursor(true);
	result->setClickedCallback(crl::guard(controller, [=] {
		ShowEventBox(controller, controller->session().user(), event, std::nullopt);
	}));
	return result;
}

Memento::Memento(not_null<PeerData*> peer)
: ContentMemento(peer, nullptr, nullptr, PeerId()) {
}

object_ptr<Info::ContentWidget> Memento::createWidget(
		QWidget *parent,
		not_null<Info::Controller*> controller,
		const QRect &geometry) {
	auto result = object_ptr<Widget>(parent, controller);
	result->setInternalState(geometry, this);
	return result;
}

Info::Section Memento::section() const {
	return Info::Section(Info::Section::Type::SeeTgHistory);
}

Widget::Widget(QWidget *parent, not_null<Info::Controller*> controller)
: ContentWidget(parent, controller)
, _peer(controller->key().peer()) {
	const auto inner = Ui::CreateChild<Inner>(
		this,
		controller->parentController(),
		_peer);
	_inner = setInnerWidget(object_ptr<Inner>::fromRaw(inner));
	_fillMenu = [inner](const Ui::Menu::MenuCallback &addAction) {
		inner->fillMenu(addAction);
	};
}

void Widget::fillTopBarMenu(const Ui::Menu::MenuCallback &addAction) {
	if (_fillMenu) {
		_fillMenu(addAction);
	}
}

not_null<PeerData*> Widget::peer() const {
	return _peer;
}

bool Widget::showInternal(not_null<Info::ContentMemento*> memento) {
	if (!controller()->validateMementoPeer(memento)) {
		return false;
	}
	if (const auto own = dynamic_cast<Memento*>(memento.get())) {
		if (own->peer() == _peer.get()) {
			scrollTopRestore(own->scrollTop());
			return true;
		}
	}
	return false;
}

void Widget::setInternalState(
		const QRect &geometry,
		not_null<Memento*> memento) {
	setGeometry(geometry);
	Ui::SendPendingMoveResizeEvents(this);
	scrollTopRestore(memento->scrollTop());
}

rpl::producer<QString> Widget::title() {
	return Lang::Value(Key::SeeTgHistoryButton);
}

std::shared_ptr<Info::ContentMemento> Widget::doCreateMemento() {
	auto result = std::make_shared<Memento>(_peer);
	result->setScrollTop(scrollTopSave());
	return result;
}

std::shared_ptr<Info::Memento> Make(not_null<PeerData*> peer) {
	return std::make_shared<Info::Memento>(
		std::vector<std::shared_ptr<Info::ContentMemento>>(
			1,
			std::make_shared<Memento>(peer)));
}

not_null<Ui::SettingsButton*> AddButton(
		not_null<Ui::VerticalLayout*> parent,
		not_null<Window::SessionNavigation*> navigation,
		not_null<PeerData*> peer,
		Ui::MultiSlideTracker &tracker) {
	const auto wrap = parent->add(
		object_ptr<Ui::SlideWrap<Ui::SettingsButton>>(
			parent,
			object_ptr<Ui::SettingsButton>(
				parent,
				Counters::Label(peer, Counters::Kind::Transfers),
				st::infoSharedMediaButton)));
	wrap->toggleOn(EnabledValue(Feature::Transfers));
	tracker.track(wrap);
	const auto button = wrap->entity();
	Counters::AddRightLabel(button, peer, Counters::Kind::Transfers);
	button->addClickHandler([=] {
		if (navigation->showFrozenError()) {
			return;
		}
		navigation->showSection(Make(peer));
	});
	return button;
}

} // namespace Fork::SeeTg::History
