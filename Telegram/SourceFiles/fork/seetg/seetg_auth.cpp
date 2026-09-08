/*
This file is part of SeeGram Desktop,
a Telegram Desktop fork.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "fork/seetg/seetg_auth.h"
#include "fork/disguise.h"

#include "apiwrap.h"
#include "base/unixtime.h"
#include "core/application.h"
#include "data/data_session.h"
#include "data/data_user.h"
#include "main/main_session.h"
#include "mtproto/sender.h"

#include <QtCore/QFile>
#include <QtCore/QJsonDocument>
#include <QtCore/QJsonObject>
#include <QtCore/QSaveFile>
#include <QtCore/QUrl>

namespace Fork::SeeTg::Auth {
namespace {

constexpr auto kBotUsername = "seetgbot";
constexpr auto kMaxAge = TimeId(3600);

struct Waiter {
	Fn<void(QString)> done;
	Fn<void(QString)> fail;
};

struct State {
	QString initData;
	TimeId obtainedAt = 0;
	UserData *bot = nullptr;
	mtpRequestId requestId = 0;
	std::vector<Waiter> waiting;
	bool loaded = false;
};

base::flat_map<not_null<Main::Session*>, std::unique_ptr<State>> States;

[[nodiscard]] QString FilePath(not_null<Main::Session*> session) {
	return cWorkingDir()
		+ u"tdata/fork_seetg_"_q
		+ QString::number(session->uniqueId())
		+ u".json"_q;
}

[[nodiscard]] State &StateFor(not_null<Main::Session*> session) {
	const auto i = States.find(session);
	if (i != end(States)) {
		return *i->second;
	}
	const auto state = States.emplace(
		session,
		std::make_unique<State>()).first->second.get();
	session->lifetime().add([=] {
		States.remove(session);
	});
	return *state;
}

void Load(not_null<Main::Session*> session, State &state) {
	state.loaded = true;
	auto file = QFile(FilePath(session));
	if (!file.open(QIODevice::ReadOnly)) {
		return;
	}
	const auto document = QJsonDocument::fromJson(file.readAll());
	if (!document.isObject()) {
		return;
	}
	const auto object = document.object();
	state.initData = object.value(u"initData"_q).toString();
	state.obtainedAt = TimeId(object.value(u"obtainedAt"_q).toDouble());
}

void Save(not_null<Main::Session*> session, const State &state) {
	auto object = QJsonObject();
	object.insert(u"initData"_q, state.initData);
	object.insert(u"obtainedAt"_q, double(state.obtainedAt));

	auto file = QSaveFile(FilePath(session));
	if (!file.open(QIODevice::WriteOnly)) {
		LOG(("SeeTg Error: cant write '%1'.").arg(FilePath(session)));
		return;
	}
	file.write(QJsonDocument(object).toJson(QJsonDocument::Compact));
	if (!file.commit()) {
		LOG(("SeeTg Error: cant commit '%1'.").arg(FilePath(session)));
	}
}

[[nodiscard]] bool Fresh(const State &state) {
	return !state.initData.isEmpty()
		&& (base::unixtime::now() - state.obtainedAt < kMaxAge);
}

// Telegram answers with the app's URL and puts the launch data into its
// fragment, percent-encoded once more on top of the query-string encoding the
// data itself carries. One decoding leaves exactly what the mini app sends
// and what the backend validates.
[[nodiscard]] QString ExtractInitData(const QString &url) {
	const auto fragment = QUrl(url).fragment(QUrl::FullyEncoded);
	const auto key = u"tgWebAppData="_q;
	auto from = fragment.indexOf(key);
	if (from < 0) {
		return QString();
	}
	from += key.size();
	auto till = fragment.indexOf('&', from);
	if (till < 0) {
		till = fragment.size();
	}
	return QUrl::fromPercentEncoding(fragment.mid(from, till - from).toUtf8());
}

void Finish(State &state, const QString &initData, const QString &error) {
	state.requestId = 0;
	if (initData.isEmpty()) {
		LOG(("SeeTg Error: sign-in failed: %1").arg(error));
	}
	auto waiting = base::take(state.waiting);
	for (const auto &waiter : waiting) {
		if (!initData.isEmpty()) {
			waiter.done(initData);
		} else {
			waiter.fail(error);
		}
	}
}

void RequestWebView(not_null<Main::Session*> session, State &state) {
	const auto bot = state.bot;
	Assert(bot != nullptr);

	state.requestId = session->api().request(MTPmessages_RequestMainWebView(
		MTP_flags(0),
		bot->input(),
		bot->inputUser(),
		MTP_string(),
		MTP_dataJSON(MTP_bytes()),
		MTP_string("tdesktop")
	)).done([=](const MTPWebViewResult &result) {
		auto &state = StateFor(session);
		const auto initData = ExtractInitData(qs(result.data().vurl()));
		if (initData.isEmpty()) {
			Finish(state, QString(), u"no launch data in the app url"_q);
			return;
		}
		state.initData = initData;
		state.obtainedAt = base::unixtime::now();
		Save(session, state);
		Finish(state, initData, QString());
	}).fail([=](const MTP::Error &error) {
		Finish(StateFor(session), QString(), error.type());
	}).send();
}

void ResolveBot(not_null<Main::Session*> session, State &state) {
	state.requestId = session->api().request(MTPcontacts_ResolveUsername(
		MTP_flags(0),
		MTP_string(kBotUsername),
		MTP_string()
	)).done([=](const MTPcontacts_ResolvedPeer &result) {
		const auto &data = result.data();
		session->data().processUsers(data.vusers());
		session->data().processChats(data.vchats());
		const auto peer = session->data().peerLoaded(
			peerFromMTP(data.vpeer()));
		auto &state = StateFor(session);
		state.bot = peer ? peer->asUser() : nullptr;
		if (!state.bot) {
			Finish(state, QString(), u"bot not found"_q);
			return;
		}
		RequestWebView(session, state);
	}).fail([=](const MTP::Error &error) {
		Finish(StateFor(session), QString(), error.type());
	}).send();
}

} // namespace

void Request(
		not_null<Main::Session*> session,
		Fn<void(QString)> done,
		Fn<void(QString)> fail) {
	if (Disguise::Clean()) {
		fail(u"integration disabled"_q);
		return;
	}
	auto &state = StateFor(session);
	if (!state.loaded) {
		Load(session, state);
	}
	if (Fresh(state)) {
		done(state.initData);
		return;
	}
	state.waiting.push_back({ std::move(done), std::move(fail) });
	if (state.requestId) {
		return;
	}
	if (state.bot) {
		RequestWebView(session, state);
	} else {
		ResolveBot(session, state);
	}
}

void Invalidate(not_null<Main::Session*> session) {
	auto &state = StateFor(session);
	state.initData = QString();
	state.obtainedAt = 0;
	state.loaded = true;
	QFile::remove(FilePath(session));
}

void CancelAll() {
	auto waiting = std::vector<Waiter>();
	for (const auto &[session, state] : States) {
		if (const auto id = base::take(state->requestId)) {
			session->api().request(id).cancel();
		}
		for (auto &waiter : state->waiting) {
			waiting.push_back(std::move(waiter));
		}
		state->waiting.clear();
	}
	for (const auto &waiter : waiting) {
		waiter.fail(u"integration disabled"_q);
	}
}

} // namespace Fork::SeeTg::Auth
