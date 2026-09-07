/*
This file is part of SeeGram Desktop,
a Telegram Desktop fork.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "fork/seetg/seetg_types.h"

#include <QtCore/QDateTime>
#include <QtCore/QJsonArray>
#include <QtCore/QUuid>

namespace Fork::SeeTg {

const char kQueryProfileNfts[] =
	"query ProfileGifts($f: GiftFilter!, $n: Int, $a: String, "
	"$by: GiftSortField, $dir: SortDirection, $o: String!) { "
	"owner(seeId: $o) { savedGiftsHidden } "
	"searchGifts(filter: $f, first: $n, after: $a, sortBy: $by, "
	"sortDir: $dir) { totalCount pageInfo { endCursor hasNextPage } "
	"items { id giftId title slug num model { name } backdrop { name } "
	"pattern { name } onSale saleInfo { amount currency } minted burned "
	"transferredAt comment "
	"sender { seeId telegramId telegramType name username } } } }";

const char kQueryProfileSaved[] =
	"query ProfileSaved($f: SavedGiftFilter!, $n: Int, $a: String, "
	"$by: SavedGiftSortField, $dir: SortDirection) { "
	"searchSavedGifts(filter: $f, first: $n, after: $a, sortBy: $by, "
	"sortDir: $dir) { totalCount pageInfo { endCursor hasNextPage } "
	"items { id giftId from { seeId telegramId telegramType name username } "
	"stars convertStars limited availabilityTotal upgradeStars nameHidden "
	"refunded visible issuedAt message } } }";

// Any one collectible a peer holds: enough to ask Telegram for that gift,
// whose answer carries the holder with an access hash.
const char kQueryAnyNft[] =
	"query AnyNft($o: String!) { "
	"searchGifts(filter: { ownerSeeId: $o }, first: 1) { "
	"items { slug num } } }";

// see.tg keeps a peer's username even when the client has never met them,
// which is what makes a username lookup possible at all here.
const char kQueryOwner[] =
	"query OwnerBySeeId($o: String!) { "
	"owner(seeId: $o) { seeId telegramId telegramType name username } }";

namespace {

[[nodiscard]] uint64 ToId(const QJsonValue &value) {
	return value.toString().toULongLong();
}

[[nodiscard]] QString NameOf(const QJsonValue &attribute) {
	return attribute.toObject().value(u"name"_q).toString();
}

} // namespace

QString SeeId(PeerId id) {
	const auto dns = QUuid(
		0x6ba7b810, 0x9dad, 0x11d1,
		0x80, 0xb4, 0x00, 0xc0, 0x4f, 0xd4, 0x30, 0xc8);
	const auto bare = id.value & PeerId::kChatTypeMask;
	const auto seed = "id:" + QByteArray::number(qulonglong(bare));
	return QUuid::createUuidV5(dns, seed).toString(QUuid::WithoutBraces);
}

Owner ParseOwner(const QJsonObject &object) {
	auto result = Owner();
	const auto bare = ToId(object.value(u"telegramId"_q));
	const auto type = object.value(u"telegramType"_q).toString();
	if (bare) {
		result.peerId = (type == u"channel"_q)
			? peerFromChannel(ChannelId(bare))
			: peerFromUser(UserId(bare));
	}
	result.seeId = object.value(u"seeId"_q).toString();
	result.name = object.value(u"name"_q).toString();
	if (result.name.isEmpty()) {
		result.name = object.value(u"title"_q).toString();
	}
	result.username = object.value(u"username"_q).toString();
	return result;
}

Nft ParseNft(const QJsonObject &object) {
	auto result = Nft();
	result.id = object.value(u"id"_q).toString();
	result.uniqueId = result.id.toULongLong();
	result.giftId = ToId(object.value(u"giftId"_q));
	result.title = object.value(u"title"_q).toString();
	result.slug = object.value(u"slug"_q).toString();
	result.num = object.value(u"num"_q).toInt();
	result.model = NameOf(object.value(u"model"_q));
	result.backdrop = NameOf(object.value(u"backdrop"_q));
	result.pattern = NameOf(object.value(u"pattern"_q));
	result.onSale = object.value(u"onSale"_q).toBool();
	const auto sale = object.value(u"saleInfo"_q).toArray();
	if (!sale.isEmpty()) {
		const auto first = sale.first().toObject();
		result.saleAmount = first.value(u"amount"_q).toString();
		result.saleCurrency = first.value(u"currency"_q).toString();
	}
	result.minted = object.value(u"minted"_q).toBool();
	result.burned = object.value(u"burned"_q).toBool();
	result.sender = ParseOwner(object.value(u"sender"_q).toObject());
	result.comment = object.value(u"comment"_q).toString();
	result.transferredAt = ParseTime(
		object.value(u"transferredAt"_q).toString());
	return result;
}

Saved ParseSaved(const QJsonObject &object) {
	auto result = Saved();
	result.id = object.value(u"id"_q).toString();
	result.giftId = ToId(object.value(u"giftId"_q));
	result.from = ParseOwner(object.value(u"from"_q).toObject());
	result.stars = object.value(u"stars"_q).toInt();
	result.convertStars = object.value(u"convertStars"_q).toInt();
	result.limited = object.value(u"limited"_q).toBool();
	result.availabilityTotal = object.value(u"availabilityTotal"_q).toInt();
	result.upgradeStars = object.value(u"upgradeStars"_q).toInt();
	result.nameHidden = object.value(u"nameHidden"_q).toBool();
	result.refunded = object.value(u"refunded"_q).toBool();
	result.visible = object.value(u"visible"_q).toBool(true);
	result.issuedAt = ParseTime(object.value(u"issuedAt"_q).toString());
	result.message = object.value(u"message"_q).toString();
	return result;
}

template <typename T, typename Parse>
[[nodiscard]] Page<T> ParsePage(const QJsonObject &connection, Parse parse) {
	auto result = Page<T>();
	for (const auto &item : connection.value(u"items"_q).toArray()) {
		result.items.push_back(parse(item.toObject()));
	}
	const auto info = connection.value(u"pageInfo"_q).toObject();
	result.endCursor = info.value(u"endCursor"_q).toString();
	result.hasNext = info.value(u"hasNextPage"_q).toBool();
	const auto total = connection.value(u"totalCount"_q);
	if (total.isDouble()) {
		result.total = total.toInt();
	}
	return result;
}

Page<Nft> ParseNftPage(const QJsonObject &connection) {
	return ParsePage<Nft>(connection, ParseNft);
}

Page<Saved> ParseSavedPage(const QJsonObject &connection) {
	return ParsePage<Saved>(connection, ParseSaved);
}

TimeId ParseTime(const QString &iso) {
	if (iso.isEmpty()) {
		return 0;
	}
	auto parsed = QDateTime::fromString(iso, Qt::ISODateWithMs);
	if (!parsed.isValid()) {
		parsed = QDateTime::fromString(iso, Qt::ISODate);
	}
	return parsed.isValid() ? TimeId(parsed.toSecsSinceEpoch()) : 0;
}

} // namespace Fork::SeeTg
