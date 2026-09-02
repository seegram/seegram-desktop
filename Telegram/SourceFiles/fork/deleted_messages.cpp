/*
This file is part of SeeGram Desktop,
a Telegram Desktop fork.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "fork/deleted_messages.h"

#include "fork/message_marks.h"
#include "fork/message_store.h"
#include "fork/spy_mode.h"
#include "base/unixtime.h"
#include "data/data_forum_topic.h"
#include "data/data_media_types.h"
#include "data/data_peer.h"
#include "data/data_session.h"
#include "data/data_user.h"
#include "history/history.h"
#include "history/history_item.h"
#include "history/history_unread_things.h"
#include "history/view/history_view_element.h"
#include "main/main_session.h"
#include "ui/painter.h"

#include <unordered_set>

namespace Fork::Deleted {
namespace {

// AyuGram's value: still readable, clearly not a live message.
constexpr auto kTranslucentOpacity = 0.7;

std::unordered_set<const HistoryItem*> Marked;

[[nodiscard]] bool Savable(not_null<HistoryItem*> item) {
	const auto &settings = Spy::Current();
	if (!settings.saveDeletedMessages
		|| !item->isRegular()
		|| item->isSponsored()) {
		return false;
	}
	const auto user = item->history()->peer->asUser();
	return !user || !user->isBot() || settings.saveForBots;
}

[[nodiscard]] Store::Record MakeRecord(not_null<HistoryItem*> item) {
	const auto media = item->media();
	return {
		.kind = Store::Record::Kind::Deleted,
		.peer = item->history()->peer->id,
		.id = item->id,
		.topicRootId = item->topicRootId(),
		.from = item->from()->id,
		.fromName = item->from()->name(),
		.date = item->date(),
		.savedAt = base::unixtime::now(),
		.text = item->originalText(),
		.mediaSummary = media ? media->notificationText() : TextWithEntities(),
	};
}

void Mark(not_null<HistoryItem*> item) {
	Marked.emplace(item.get());

	const auto history = item->history();
	const auto id = item->id;

	// A kept message can never be read again as far as the server knows, so
	// an unread mention or reaction on it would stay unread forever.
	if (item->isUnreadMention()) {
		history->unreadMentions().erase(id);
		if (const auto topic = item->topic()) {
			topic->unreadMentions().erase(id);
		}
	}
	if (item->hasUnreadReaction()) {
		history->unreadReactions().erase(id);
		if (const auto topic = item->topic()) {
			topic->unreadReactions().erase(id);
		}
	}

	// A self-destructing message is kept too. Its timer stays armed inside
	// the item, so it is taken off the session's schedule by hand: left
	// there, the next check would find it expired again, and again.
	if (const auto when = item->ttlDestroyAt()) {
		history->owner().unregisterMessageTTL(when, item);
	}

	history->owner().requestItemViewRefresh(item);
	history->owner().requestItemResize(item);
}

} // namespace

bool Is(not_null<const HistoryItem*> item) {
	return Marked.contains(item.get());
}

bool Intercept(not_null<HistoryItem*> item) {
	if (Is(item)) {
		return true;
	} else if (!Savable(item)) {
		return false;
	}
	Store::For(&item->history()->session()).add(MakeRecord(item));
	Mark(item);
	return true;
}

void InterceptAll(std::vector<not_null<HistoryItem*>> &items) {
	items.erase(
		ranges::remove_if(items, [](not_null<HistoryItem*> item) {
			return Intercept(item);
		}),
		end(items));
}

bool TakeLocal(not_null<HistoryItem*> item) {
	if (!Is(item)) {
		return false;
	}
	Store::For(&item->history()->session()).removeDeleted(item->fullId());
	return true;
}

void Forget(not_null<const HistoryItem*> item) {
	Marked.erase(item.get());
}

void Clear(not_null<PeerData*> peer, MsgId topicRootId) {
	auto items = std::vector<not_null<HistoryItem*>>();
	const auto history = peer->owner().history(peer);
	for (const auto &block : history->blocks) {
		for (const auto &view : block->messages) {
			const auto item = view->data();
			if (Is(item)
				&& (!topicRootId || item->topicRootId() == topicRootId)) {
				items.push_back(item);
			}
		}
	}
	Store::For(&peer->session()).clearDeleted(peer->id, topicRootId);
	for (const auto &item : items) {
		item->destroy();
	}
}

FadeGuard::FadeGuard(Painter &p, not_null<const HistoryItem*> item)
: _p(p)
, _saved(p.opacity()) {
	if (Marks::TranslucentDeleted() && Is(item)) {
		_p.setOpacity(_saved * kTranslucentOpacity);
		_applied = true;
	}
}

FadeGuard::~FadeGuard() {
	if (_applied) {
		_p.setOpacity(_saved);
	}
}

} // namespace Fork::Deleted
