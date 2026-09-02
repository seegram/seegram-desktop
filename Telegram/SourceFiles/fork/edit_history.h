/*
This file is part of SeeGram Desktop,
a Telegram Desktop fork.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

// Earlier texts of edited messages.
//
// Each time the server announces an edit, the text the message had until now
// is written to fork/message_store.h before the edit is applied, so that the
// whole chain of versions can be read back later. Following AyuGram, edits
// of one's own messages are not kept - their author already knows - and
// neither are hidden edits, which change reactions and views, not text.

class HistoryItem;

namespace Fork::Edits {

// Hook, called from Data::Session before an edit is applied.
void Record(not_null<HistoryItem*> item, const MTPMessage &data);

[[nodiscard]] bool HasRevisions(not_null<HistoryItem*> item);

} // namespace Fork::Edits
