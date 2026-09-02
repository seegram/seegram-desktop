/*
This file is part of SeeGram Desktop,
a Telegram Desktop fork.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "fork/message_store.h"

#include "core/application.h"
#include "main/main_session.h"

#include <QtCore/QDataStream>
#include <QtCore/QFile>
#include <QtCore/QSaveFile>

namespace Fork::Store {
namespace {

constexpr auto kMagic = quint32(0x5347'4D53); // "SGMS".
constexpr auto kFormatVersion = quint32(1);
constexpr auto kStreamVersion = QDataStream::Qt_5_1;
constexpr auto kMaxEntities = 65536;

base::flat_map<not_null<Main::Session*>, std::unique_ptr<Account>> Accounts;

[[nodiscard]] QString PathFor(not_null<Main::Session*> session) {
	return cWorkingDir()
		+ u"tdata/fork_messages_"_q
		+ QString::number(session->uniqueId())
		+ u".dat"_q;
}

[[nodiscard]] bool MatchesTopic(const Record &record, MsgId topicRootId) {
	return !topicRootId || (record.topicRootId == topicRootId);
}

void WriteText(QDataStream &stream, const TextWithEntities &text) {
	stream << text.text << qint32(text.entities.size());
	for (const auto &entity : text.entities) {
		stream
			<< quint8(entity.type())
			<< qint32(entity.offset())
			<< qint32(entity.length())
			<< entity.data();
	}
}

[[nodiscard]] bool ReadText(QDataStream &stream, TextWithEntities &text) {
	auto count = qint32();
	stream >> text.text >> count;
	if (stream.status() != QDataStream::Ok
		|| count < 0
		|| count > kMaxEntities) {
		return false;
	}
	text.entities.reserve(count);
	for (auto i = 0; i != count; ++i) {
		auto type = quint8();
		auto offset = qint32();
		auto length = qint32();
		auto data = QString();
		stream >> type >> offset >> length >> data;
		if (stream.status() != QDataStream::Ok) {
			return false;
		}
		text.entities.push_back(
			EntityInText(EntityType(type), offset, length, data));
	}
	return true;
}

void WriteRecord(QDataStream &stream, const Record &record) {
	stream
		<< quint8(record.kind)
		<< quint64(record.peer.value)
		<< qint64(record.id.bare)
		<< qint64(record.topicRootId.bare)
		<< quint64(record.from.value)
		<< record.fromName
		<< qint32(record.date)
		<< qint32(record.savedAt);
	WriteText(stream, record.text);
	WriteText(stream, record.mediaSummary);
}

[[nodiscard]] bool ReadRecord(QDataStream &stream, Record &record) {
	auto kind = quint8();
	auto peer = quint64();
	auto id = qint64();
	auto topicRootId = qint64();
	auto from = quint64();
	auto date = qint32();
	auto savedAt = qint32();
	stream
		>> kind
		>> peer
		>> id
		>> topicRootId
		>> from
		>> record.fromName
		>> date
		>> savedAt;
	if (stream.status() != QDataStream::Ok
		|| kind > quint8(Record::Kind::Edited)) {
		return false;
	}
	record.kind = Record::Kind(kind);
	record.peer = PeerId(peer);
	record.id = MsgId(id);
	record.topicRootId = MsgId(topicRootId);
	record.from = PeerId(from);
	record.date = date;
	record.savedAt = savedAt;
	return ReadText(stream, record.text)
		&& ReadText(stream, record.mediaSummary);
}

void WriteHeader(QDataStream &stream) {
	stream << kMagic << kFormatVersion;
}

} // namespace

Account::Account(not_null<Main::Session*> session)
: _path(PathFor(session)) {
	load();
}

void Account::load() {
	auto file = QFile(_path);
	if (!file.open(QIODevice::ReadOnly)) {
		return;
	}
	auto stream = QDataStream(&file);
	stream.setVersion(kStreamVersion);
	auto magic = quint32();
	auto version = quint32();
	stream >> magic >> version;
	if (stream.status() != QDataStream::Ok
		|| magic != kMagic
		|| version != kFormatVersion) {
		LOG(("Store Error: '%1' has an unknown format, ignoring it."
			).arg(_path));
		return;
	}
	while (!stream.atEnd()) {
		auto record = Record();
		if (!ReadRecord(stream, record)) {
			LOG(("Store Error: '%1' is damaged, keeping what was read."
				).arg(_path));
			break;
		}
		if (record.kind == Record::Kind::Deleted) {
			_deleted.push_back(std::move(record));
		} else {
			_edits[record.fullId()].push_back(std::move(record));
		}
	}
}

void Account::add(Record &&record) {
	append(record);
	if (record.kind == Record::Kind::Deleted) {
		_deleted.push_back(std::move(record));
	} else {
		_edits[record.fullId()].push_back(std::move(record));
	}
}

std::vector<Record> Account::deleted(PeerId peer, MsgId topicRootId) const {
	auto result = std::vector<Record>();
	for (const auto &record : _deleted) {
		if (record.peer == peer && MatchesTopic(record, topicRootId)) {
			result.push_back(record);
		}
	}
	return result;
}

bool Account::hasDeleted(PeerId peer, MsgId topicRootId) const {
	return ranges::any_of(_deleted, [&](const Record &record) {
		return (record.peer == peer) && MatchesTopic(record, topicRootId);
	});
}

const std::vector<Record> *Account::edits(FullMsgId id) const {
	const auto i = _edits.find(id);
	return (i != end(_edits)) ? &i->second : nullptr;
}

void Account::removeDeleted(FullMsgId id) {
	const auto removed = std::erase_if(_deleted, [&](const Record &record) {
		return (record.fullId() == id);
	});
	if (removed) {
		rewrite();
	}
}

void Account::clearDeleted(PeerId peer, MsgId topicRootId) {
	const auto removed = std::erase_if(_deleted, [&](const Record &record) {
		return (record.peer == peer) && MatchesTopic(record, topicRootId);
	});
	if (removed) {
		rewrite();
	}
}

void Account::append(const Record &record) {
	auto file = QFile(_path);
	if (!file.open(QIODevice::WriteOnly | QIODevice::Append)) {
		LOG(("Store Error: cant write '%1'.").arg(_path));
		return;
	}
	auto stream = QDataStream(&file);
	stream.setVersion(kStreamVersion);
	if (!file.size()) {
		WriteHeader(stream);
	}
	WriteRecord(stream, record);
}

void Account::rewrite() {
	auto file = QSaveFile(_path);
	if (!file.open(QIODevice::WriteOnly)) {
		LOG(("Store Error: cant rewrite '%1'.").arg(_path));
		return;
	}
	auto stream = QDataStream(&file);
	stream.setVersion(kStreamVersion);
	WriteHeader(stream);
	for (const auto &record : _deleted) {
		WriteRecord(stream, record);
	}
	for (const auto &[id, records] : _edits) {
		for (const auto &record : records) {
			WriteRecord(stream, record);
		}
	}
	if (!file.commit()) {
		LOG(("Store Error: cant commit '%1'.").arg(_path));
	}
}

Account &For(not_null<Main::Session*> session) {
	const auto i = Accounts.find(session);
	if (i != end(Accounts)) {
		return *i->second;
	}
	const auto account = Accounts.emplace(
		session,
		std::make_unique<Account>(session)).first->second.get();
	session->lifetime().add([=] {
		Accounts.remove(session);
	});
	return *account;
}

} // namespace Fork::Store
