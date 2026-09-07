/*
This file is part of SeeGram Desktop,
a Telegram Desktop fork.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include <QtCore/QJsonObject>

// One GraphQL operation against the see.tg backend, signed with the
// account's initData (see fork/seetg/seetg_auth.h).
//
// Query merges identical requests in flight and keeps answers for a few
// minutes. FreshQuery and Mutation bypass both mechanisms: a read following
// a write needs current data, and distinct writes must reach the server.
// Only an HTTP 401 triggers a single retry after reauthentication. A network
// failure does not prove a mutation was rejected, so it must not be replayed.
//
// Callbacks are not guarded here; wrap them in crl::guard at the call site
// when the caller can die first.

namespace Main {
class Session;
} // namespace Main

namespace Fork::SeeTg::Api {

struct Error {
	enum class Kind {
		Auth,
		RateLimited,
		Premium,
		Network,
		Other,
	};
	Kind kind = Kind::Other;
	QString message;
};

using Done = Fn<void(const QJsonObject &data)>;
using Fail = Fn<void(const Error &error)>;

void Query(
	not_null<Main::Session*> session,
	const QString &document,
	const QJsonObject &variables,
	Done done,
	Fail fail);

void FreshQuery(
	not_null<Main::Session*> session,
	const QString &document,
	const QJsonObject &variables,
	Done done,
	Fail fail);

void Mutation(
	not_null<Main::Session*> session,
	const QString &document,
	const QJsonObject &variables,
	Done done,
	Fail fail);

// Drops the answers kept for this account.
void ClearCache(not_null<Main::Session*> session);

} // namespace Fork::SeeTg::Api
