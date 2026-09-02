/*
This file is part of SeeGram Desktop,
a Telegram Desktop fork.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "fork/edit_history.h"

#include "fork/message_store.h"
#include "fork/spy_mode.h"
#include "api/api_text_entities.h"
#include "data/data_peer.h"
#include "history/history.h"
#include "history/history_item.h"
#include "history/history_item_components.h"
#include "main/main_session.h"

namespace Fork::Edits {

void Record(not_null<HistoryItem*> item, const MTPMessage &data) {
	if (!Spy::Current().saveEditsHistory
		|| data.type() != mtpc_message
		|| !item->isRegular()
		|| item->author()->isSelf()) {
		return;
	}
	const auto &message = data.c_message();
	if (message.is_edit_hide()) {
		return;
	}
	const auto session = &item->history()->session();
	const auto edited = TextWithEntities{
		qs(message.vmessage()),
		Api::EntitiesFromMTP(
			session,
			message.ventities().value_or_empty()),
	};
	const auto &current = item->originalText();
	if (current.empty() || edited == current) {
		return;
	}
	const auto previous = item->Get<HistoryMessageEdited>();
	Store::For(session).add({
		.kind = Store::Record::Kind::Edited,
		.peer = item->history()->peer->id,
		.id = item->id,
		.topicRootId = item->topicRootId(),
		.from = item->from()->id,
		.fromName = item->from()->name(),
		.date = item->date(),
		.savedAt = previous ? previous->date : TimeId(0),
		.text = current,
	});
}

bool HasRevisions(not_null<HistoryItem*> item) {
	return item->isRegular()
		&& (Store::For(&item->history()->session()).edits(item->fullId())
			!= nullptr);
}

} // namespace Fork::Edits
