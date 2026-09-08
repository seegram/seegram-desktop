/*
This file is part of SeeGram Desktop, a Telegram Desktop fork.
For license and copyright information see:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "fork/ghost_notifications.h"
#include "fork/ghost_mode.h"
#include "base/unixtime.h"
#include "core/application.h"
#include "data/data_session.h"
#include "window/notifications_manager.h"
#include "data/components/scheduled_messages.h"
#include "history/history.h"
#include "history/history_item.h"
#include "main/main_session.h"
#include <QtCore/QFile>
#include <QtCore/QSaveFile>
#include <QtCore/QJsonDocument>
#include <QtCore/QJsonObject>

namespace Fork::GhostNotifications {
namespace {
struct State {
	QJsonObject scheduled;
	QJsonObject sent;
};
base::flat_map<Main::Session*, State> States;
base::flat_set<const HistoryItem*> Pending;
QString Path(not_null<Main::Session*> session) {
	return cWorkingDir() + u"tdata/fork_ghost_notifications_"_q
		+ QString::number(session->uniqueId()) + u".json"_q;
}
QString Key(PeerId peer, MsgId id) {
	return QString::number(peer.value) + ':' + QString::number(id.bare);
}
void Save(not_null<Main::Session*> session, const State &state) {
	auto file = QSaveFile(Path(session));
	if (file.open(QIODevice::WriteOnly)) {
		file.write(QJsonDocument(QJsonObject{
			{ u"scheduled"_q, state.scheduled }, { u"sent"_q, state.sent }
		}).toJson(QJsonDocument::Compact));
		file.commit();
	}
}
State &Get(not_null<Main::Session*> session) {
	const auto [i, added] = States.emplace(session.get(), State());
	if (added) {
		auto file = QFile(Path(session));
		if (file.open(QIODevice::ReadOnly)) {
			const auto data = QJsonDocument::fromJson(file.readAll()).object();
			i->second.scheduled = data.value(u"scheduled"_q).toObject();
			i->second.sent = data.value(u"sent"_q).toObject();
		}
		// Keep delivered IDs for a bounded retention period after restart.
		const auto cutoff = base::unixtime::now() - 30 * 86400;
		for (auto j = i->second.sent.begin(); j != i->second.sent.end();) {
			j = (j.value().toDouble() < cutoff) ? i->second.sent.erase(j) : ++j;
		}
		session->lifetime().add([=] { States.remove(session.get()); });
	}
	return i->second;
}
} // namespace

void Refresh(not_null<HistoryItem*> item) {
	if (!Pending.contains(item.get()) || item->isSending() || item->hasFailed()
		|| !item->isScheduled() || !Data::IsScheduledMsgId(item->id)) {
		return;
	}
	Pending.remove(item.get());
	const auto session = &item->history()->session();
	auto &state = Get(session);
	const auto id = session->scheduledMessages().lookupId(item);
	state.scheduled.insert(Key(item->history()->peer->id, id), true);
	Save(session, state);
}
void Remember(not_null<HistoryItem*> item) {
	Pending.emplace(item.get());
	Refresh(item);
}
void Rebind(not_null<HistoryItem*> from, HistoryItem *to) {
	if (Pending.remove(from.get()) && to) {
		Remember(to);
	}
}
void Forget(not_null<const HistoryItem*> item) {
	Pending.remove(item.get());
}
void Apply(not_null<Main::Session*> session,
		const MTPDupdateDeleteScheduledMessages &update) {
	auto &state = Get(session);
	const auto peer = peerFromMTP(update.vpeer());
	const auto &ids = update.vmessages().v;
	const auto sent = update.vsent_messages();
	auto changed = false;
	auto delivered = std::vector<FullMsgId>();
	for (auto i = 0; i != ids.size(); ++i) {
		const auto key = Key(peer, ids[i].v);
		if (!state.scheduled.contains(key)) {
			continue;
		}
		state.scheduled.remove(key);
		if (sent && i < sent->v.size()) {
			state.sent.insert(Key(peer, sent->v[i].v), double(base::unixtime::now()));
			delivered.push_back({ peer, sent->v[i].v });
		}
		changed = true;
	}
	if (changed) {
		Save(session, state);
		for (const auto id : delivered) {
			if (const auto item = session->data().message(id); item && Suppress(item)) {
				Core::App().notifications().clearFromItem(item);
			}
		}
	}
}
void Prepare(not_null<Main::Session*> session, const MTPVector<MTPUpdate> &updates) {
	for (const auto &update : updates.v) {
		if (update.type() == mtpc_updateDeleteScheduledMessages) {
			Apply(session, update.c_updateDeleteScheduledMessages());
		}
	}
}
bool Suppress(not_null<const HistoryItem*> item) {
	return Ghost::Current().muteScheduledNotifications
		&& item->isFromScheduled()
		&& (item->out() || item->history()->peer->isSelf())
		&& Get(&item->history()->session()).sent.contains(Key(item->fullId().peer, item->id));
}
} // namespace Fork::GhostNotifications
