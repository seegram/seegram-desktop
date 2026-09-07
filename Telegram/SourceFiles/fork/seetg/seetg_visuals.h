/*
This file is part of SeeGram Desktop,
a Telegram Desktop fork.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

// What a collectible looks like when Telegram has not told the client: the
// backdrop colours and the model picture, from the changes.tg catalog the
// see.tg mini app draws its cards from. Used only for gifts the profile's
// own Telegram list does not carry - the hidden ones - so this is a
// fallback, not the main path.

namespace Fork::SeeTg::Visuals {

struct Backdrop {
	QColor center;
	QColor edge;
	QColor pattern;
	QColor text;
};

// Fetched once per process and kept; the callback runs when the table is
// known, at once if it already is.
void Backdrops(Fn<void()> done);
[[nodiscard]] std::optional<Backdrop> BackdropByName(const QString &name);

// The model's picture for a collection. Kept in memory once loaded and on
// disk across restarts; the callback runs at once when the image is cached.
[[nodiscard]] QString ModelImageUrl(uint64 giftId, const QString &model);
[[nodiscard]] QString OriginalImageUrl(uint64 giftId);

// The catalogues the pickers list, all from changes.tg and cached on disk:
// the upgradable collections, a collection's models, every pattern by
// collection, and the backdrops in their own order with their colours.
struct Collection {
	uint64 id = 0;
	QString name;
};
void Collections(Fn<void(const std::vector<Collection>&)> done);
void Models(uint64 giftId, Fn<void(QStringList)> done);
void Patterns(Fn<void()> done);
[[nodiscard]] QStringList PatternNames(const QStringList &collections);
[[nodiscard]] std::vector<std::pair<QString, Backdrop>> AllBackdrops();

// The collection's own animation (the .tgs Telegram plays), for a gift the
// shop catalog no longer carries.
[[nodiscard]] QString OriginalAnimationUrl(uint64 giftId);

// Any file by url, through the same disk cache as the images; an empty
// array when it could not be fetched.
void Fetch(const QString &url, Fn<void(QByteArray)> done);

// The pattern as an alpha mask, addressed by the collection's title.
[[nodiscard]] QString PatternUrl(const QString &title, const QString &pattern);

// Telegram serves a public picture for anyone with a username, which is
// the only way to show a face for a peer the client has no key for.
[[nodiscard]] QString UserpicUrl(const QString &username);
void Image(const QString &url, Fn<void(QImage)> done);

} // namespace Fork::SeeTg::Visuals
