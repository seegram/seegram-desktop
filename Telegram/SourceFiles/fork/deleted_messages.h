/*
This file is part of SeeGram Desktop,
a Telegram Desktop fork.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "data/data_msg_id.h"

// Deleted messages that stay in the chat.
//
// When the server says a message is gone, the client normally destroys its
// HistoryItem. With spy mode on the item is kept instead, remembered here as
// deleted, marked as such next to its time and, optionally, painted
// translucent. The text is also written to fork/message_store.h, so that the
// message can still be looked up after the chat is reloaded or the client
// restarted - by then the item itself is gone, and only the store remains.
//
// This follows AyuGram, which keeps a flag inside HistoryItem. This fork
// keeps the set of deleted items on the side instead: HistoryItem's flags
// are all taken, and a field added there would collide on every rebase.
// The set is keyed by pointer and emptied from the item's destructor.

class HistoryItem;
class PeerData;
class Painter;

namespace Fork::Deleted {

[[nodiscard]] bool Is(not_null<const HistoryItem*> item);

// Called where the server has deleted an item and the client is about to
// destroy it. Returns true when the item was kept instead.
[[nodiscard]] bool Intercept(not_null<HistoryItem*> item);

// Same, for a batch: the kept items are removed from the list.
void InterceptAll(std::vector<not_null<HistoryItem*>> &items);

// Called when the user deletes a message locally. Returns true when it was
// one of the kept ones: the server has nothing to delete, and the saved
// text goes with it.
[[nodiscard]] bool TakeLocal(not_null<HistoryItem*> item);

// Hook, called from HistoryItem's destructor.
void Forget(not_null<const HistoryItem*> item);

// Destroys every kept item in a chat and drops the saved texts.
void Clear(not_null<PeerData*> peer, MsgId topicRootId);

// Sets the painter's opacity for a deleted message and restores it when the
// message is drawn. Does nothing unless translucency is on.
class FadeGuard final {
public:
	FadeGuard(Painter &p, not_null<const HistoryItem*> item);
	FadeGuard(const FadeGuard &) = delete;
	FadeGuard &operator=(const FadeGuard &) = delete;
	~FadeGuard();

private:
	Painter &_p;
	const float64 _saved = 1.;
	bool _applied = false;

};

} // namespace Fork::Deleted
