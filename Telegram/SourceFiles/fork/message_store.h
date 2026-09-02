/*
This file is part of SeeGram Desktop,
a Telegram Desktop fork.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "data/data_msg_id.h"
#include "data/data_peer_id.h"
#include "ui/text/text_entity.h"

// What spy mode remembers across restarts: the text of every message the
// server deleted, and every earlier text of every message that was edited.
//
// One append-only file per account under tdata, in a QDataStream format of
// its own. AyuGram keeps the same data in an SQLite database; this fork has
// no SQLite and would rather not add a dependency for two tables that are
// only ever read as a whole. The file is read once when the account's
// session starts, kept in memory, appended to on every save, and rewritten
// only when something is removed - which happens on the user's request and
// so may cost a full rewrite.
//
// Only the text is kept, as AyuGram does today: media is a file on the
// server, and the server is exactly what has just forgotten the message.

namespace Main {
class Session;
} // namespace Main

namespace Fork::Store {

struct Record {
	enum class Kind : quint8 {
		Deleted = 0,
		Edited = 1,
	};

	Kind kind = Kind::Deleted;
	PeerId peer;
	MsgId id;
	MsgId topicRootId;
	PeerId from;
	QString fromName;
	TimeId date = 0;

	// For a deleted message: when it was deleted. For an edited one: the
	// edit date of the version kept here, zero for the original text.
	TimeId savedAt = 0;

	TextWithEntities text;
	TextWithEntities mediaSummary;

	[[nodiscard]] FullMsgId fullId() const {
		return { peer, id };
	}
};

class Account final {
public:
	explicit Account(not_null<Main::Session*> session);

	void add(Record &&record);

	[[nodiscard]] std::vector<Record> deleted(
		PeerId peer,
		MsgId topicRootId) const;
	[[nodiscard]] bool hasDeleted(PeerId peer, MsgId topicRootId) const;
	[[nodiscard]] const std::vector<Record> *edits(FullMsgId id) const;

	void removeDeleted(FullMsgId id);
	void clearDeleted(PeerId peer, MsgId topicRootId);

private:
	void load();
	void append(const Record &record);
	void rewrite();

	const QString _path;
	std::vector<Record> _deleted;
	base::flat_map<FullMsgId, std::vector<Record>> _edits;

};

// Created on first use and destroyed with the session.
[[nodiscard]] Account &For(not_null<Main::Session*> session);

} // namespace Fork::Store
