/*
This file is part of SeeGram Desktop,
a Telegram Desktop fork.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "data/data_peer_id.h"

#include <QtCore/QJsonObject>

// The slice of the see.tg schema the client reads, and the parsers for it.
// Introspection is off on the backend, so the query documents below are the
// only place the shapes are written down on this side; they mirror the ones
// the see.tg mini app sends.

namespace Fork::SeeTg {

struct Owner {
	PeerId peerId = 0;
	QString seeId;
	QString name;
	QString username;

	[[nodiscard]] bool known() const {
		return peerId != 0;
	}
};

// An upgraded (collectible) gift.
struct Nft {
	QString id;
	uint64 uniqueId = 0; // id as a number, what Telegram calls the collectible id
	uint64 giftId = 0;
	QString title;
	QString slug;
	int num = 0;
	QString model;
	QString backdrop;
	QString pattern;
	bool onSale = false;
	QString saleAmount;
	QString saleCurrency;
	bool minted = false;
	bool burned = false;
	Owner sender;
	QString comment;
	TimeId transferredAt = 0;

	// The slug Telegram itself uses in t.me/nft/<slug> links.
	[[nodiscard]] QString nftSlug() const {
		return slug + '-' + QString::number(num);
	}
};

// A non-upgraded gift, limited or regular.
struct Saved {
	QString id;
	uint64 giftId = 0;
	Owner from;
	int stars = 0;
	int convertStars = 0;
	bool limited = false;
	int availabilityTotal = 0;
	int upgradeStars = 0;
	bool nameHidden = false;
	bool refunded = false;
	bool visible = true;
	TimeId issuedAt = 0;
	QString message;
};

template <typename T>
struct Page {
	std::vector<T> items;
	QString endCursor;
	bool hasNext = false;
	std::optional<int> total;
};

// see.tg's stable owner id: UUIDv5 over "id:<telegram id>".
[[nodiscard]] QString SeeId(PeerId id);

[[nodiscard]] Owner ParseOwner(const QJsonObject &object);
[[nodiscard]] Nft ParseNft(const QJsonObject &object);
[[nodiscard]] Saved ParseSaved(const QJsonObject &object);
[[nodiscard]] Page<Nft> ParseNftPage(const QJsonObject &connection);
[[nodiscard]] Page<Saved> ParseSavedPage(const QJsonObject &connection);
[[nodiscard]] TimeId ParseTime(const QString &iso);

extern const char kQueryProfileNfts[];
extern const char kQueryProfileSaved[];
extern const char kQueryAnyNft[];
extern const char kQueryOwner[];

} // namespace Fork::SeeTg
