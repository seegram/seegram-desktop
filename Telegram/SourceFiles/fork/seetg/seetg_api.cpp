/*
This file is part of SeeGram Desktop,
a Telegram Desktop fork.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "fork/seetg/seetg_api.h"

#include "fork/seetg/seetg_auth.h"
#include "fork/seetg/seetg_http.h"
#include "core/version.h"
#include "main/main_session.h"

#include <QtCore/QJsonArray>
#include <QtCore/QJsonDocument>

namespace Fork::SeeTg::Api {
namespace {

constexpr auto kEndpoint = "https://test-backend.see.tg/graphql";
constexpr auto kTimeout = 15 * crl::time(1000);
constexpr auto kCacheTtl = 5 * 60 * crl::time(1000);
constexpr auto kCacheLimit = 64;

struct Waiter {
	Done done;
	Fail fail;
};

struct Cached {
	QJsonObject data;
	crl::time at = 0;
};

// Keyed by account and by the exact operation, so two profiles never share
// an answer and two accounts never share a session.
base::flat_map<QString, Cached> Cache;
base::flat_map<QString, std::vector<Waiter>> InFlight;

[[nodiscard]] QString CacheKey(
		not_null<Main::Session*> session,
		const QString &document,
		const QJsonObject &variables) {
	return QString::number(session->uniqueId())
		+ '\n'
		+ document
		+ '\n'
		+ QString::fromUtf8(
			QJsonDocument(variables).toJson(QJsonDocument::Compact));
}

void Trim() {
	if (Cache.size() <= kCacheLimit) {
		return;
	}
	auto oldest = begin(Cache);
	for (auto i = begin(Cache); i != end(Cache); ++i) {
		if (i->second.at < oldest->second.at) {
			oldest = i;
		}
	}
	Cache.erase(oldest);
}

void Resolve(const QString &key, const QJsonObject *data, const Error *error) {
	const auto i = InFlight.find(key);
	if (i == end(InFlight)) {
		return;
	}
	auto waiters = std::move(i->second);
	InFlight.erase(i);
	if (error) {
		LOG(("SeeTg Error: request failed: %1").arg(error->message));
	}
	for (const auto &waiter : waiters) {
		if (data) {
			waiter.done(*data);
		} else {
			waiter.fail(*error);
		}
	}
}

[[nodiscard]] Error::Kind KindFromMessage(const QString &message) {
	const auto lower = message.toLower();
	return (lower == u"unauthorized"_q)
		? Error::Kind::Auth
		: (lower.contains(u"rate limit"_q) || lower.contains(u"limit"_q))
		? Error::Kind::RateLimited
		: (lower == u"premium"_q)
		? Error::Kind::Premium
		: Error::Kind::Other;
}

void Send(
		not_null<Main::Session*> session,
		const QString &key,
		const QString &document,
		const QJsonObject &variables,
		bool retried);

void SendSigned(
		not_null<Main::Session*> session,
		const QString &key,
		const QString &document,
		const QJsonObject &variables,
		const QString &initData,
		bool retried) {
	auto body = QJsonObject();
	body.insert(u"query"_q, document);
	body.insert(u"variables"_q, variables);

	const auto headers = std::vector<Http::Header>{
		{ "Content-Type", "application/json" },
		{ "Authorization", ("tma " + initData).toUtf8() },
		{
			"User-Agent",
			("SeeGram Desktop/" + QString::fromLatin1(AppVersionStr)).toUtf8(),
		},
	};
	Http::Post(
		QString::fromLatin1(kEndpoint),
		headers,
		QJsonDocument(body).toJson(QJsonDocument::Compact),
		kTimeout,
		[=](Http::Response response) {
			const auto status = response.status;
			if (status == 401 && !retried) {
				Auth::Invalidate(session);
				Send(session, key, document, variables, true);
				return;
			} else if (!status) {
				const auto error = Error{
					Error::Kind::Network,
					response.error,
				};
				Resolve(key, nullptr, &error);
				return;
			}
			const auto json = QJsonDocument::fromJson(response.body).object();
			const auto errors = json.value(u"errors"_q).toArray();
			if (status == 401) {
				const auto error = Error{ Error::Kind::Auth, u"unauthorized"_q };
				Resolve(key, nullptr, &error);
			} else if (status == 429) {
				const auto error = Error{
					Error::Kind::RateLimited,
					u"rate limited"_q,
				};
				Resolve(key, nullptr, &error);
			} else if (!errors.isEmpty()) {
				const auto message = errors.first().toObject().value(
					u"message"_q).toString();
				const auto error = Error{ KindFromMessage(message), message };
				Resolve(key, nullptr, &error);
			} else if (status < 200
				|| status >= 300
				|| !json.contains(u"data"_q)) {
				const auto error = Error{
					Error::Kind::Other,
					u"HTTP "_q + QString::number(status),
				};
				Resolve(key, nullptr, &error);
			} else {
				const auto data = json.value(u"data"_q).toObject();
				Cache[key] = { data, crl::now() };
				Trim();
				Resolve(key, &data, nullptr);
			}
		});
}

void Send(
		not_null<Main::Session*> session,
		const QString &key,
		const QString &document,
		const QJsonObject &variables,
		bool retried) {
	Auth::Request(session, [=](QString initData) {
		SendSigned(session, key, document, variables, initData, retried);
	}, [=](QString reason) {
		const auto error = Error{ Error::Kind::Auth, reason };
		Resolve(key, nullptr, &error);
	});
}

} // namespace

void Query(
		not_null<Main::Session*> session,
		const QString &document,
		const QJsonObject &variables,
		Done done,
		Fail fail) {
	const auto key = CacheKey(session, document, variables);
	if (const auto i = Cache.find(key); i != end(Cache)) {
		if (crl::now() - i->second.at < kCacheTtl) {
			done(i->second.data);
			return;
		}
		Cache.erase(i);
	}
	const auto pending = InFlight.contains(key);
	InFlight[key].push_back({ std::move(done), std::move(fail) });
	if (!pending) {
		Send(session, key, document, variables, false);
	}
}

void ClearCache(not_null<Main::Session*> session) {
	const auto prefix = QString::number(session->uniqueId()) + '\n';
	for (auto i = begin(Cache); i != end(Cache);) {
		if (i->first.startsWith(prefix)) {
			i = Cache.erase(i);
		} else {
			++i;
		}
	}
}

} // namespace Fork::SeeTg::Api
