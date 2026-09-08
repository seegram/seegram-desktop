/*
This file is part of SeeGram Desktop,
a Telegram Desktop fork.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "fork/spy_mode.h"
#include "fork/disguise.h"

#include "core/application.h"
#include "history/history_item.h"
#include "data/data_media_types.h"

#include <rpl/event_stream.h>

#include <QtCore/QFile>
#include <QtCore/QJsonDocument>
#include <QtCore/QJsonObject>
#include <QtCore/QSaveFile>

namespace Fork::Spy {
namespace {

constexpr auto kFileName = "tdata/fork_spy.json";

Settings GlobalSettings;
rpl::event_stream<Settings> GlobalChanges;

[[nodiscard]] QString FilePath() {
	return cWorkingDir() + QString::fromLatin1(kFileName);
}

} // namespace

const Settings &Current() {
	static const auto clean = Settings{ .saveDeletedMessages = false, .saveEditsHistory = false, .previewSelfDestructMedia = false };
	return Disguise::Clean() ? clean : GlobalSettings;
}

void Start() {
	auto file = QFile(FilePath());
	if (!file.open(QIODevice::ReadOnly)) {
		return; // No file yet: the defaults above apply.
	}
	const auto document = QJsonDocument::fromJson(file.readAll());
	if (!document.isObject()) {
		LOG(("Spy Error: '%1' is not a JSON object, ignoring it."
			).arg(FilePath()));
		return;
	}
	const auto object = document.object();
	const auto read = [&](const char *key, bool fallback) {
		const auto value = object.value(QLatin1String(key));
		return value.isBool() ? value.toBool() : fallback;
	};
	const auto defaults = Settings();
	GlobalSettings.saveDeletedMessages = read(
		"saveDeletedMessages",
		defaults.saveDeletedMessages);
	GlobalSettings.saveEditsHistory = read(
		"saveEditsHistory",
		defaults.saveEditsHistory);
	GlobalSettings.saveForBots = read("saveForBots", defaults.saveForBots);
	GlobalSettings.previewSelfDestructMedia = read("previewSelfDestructMedia", true);
}

void Set(const Settings &settings) {
	if (Disguise::Clean() || GlobalSettings == settings) {
		return;
	}
	GlobalSettings = settings;
	GlobalChanges.fire_copy(settings);

	auto object = QJsonObject();
	object.insert(u"saveDeletedMessages"_q, settings.saveDeletedMessages);
	object.insert(u"saveEditsHistory"_q, settings.saveEditsHistory);
	object.insert(u"saveForBots"_q, settings.saveForBots);
	object.insert(u"previewSelfDestructMedia"_q, settings.previewSelfDestructMedia);

	// QSaveFile so that a crash mid-write cannot leave a truncated file.
	auto file = QSaveFile(FilePath());
	if (!file.open(QIODevice::WriteOnly)) {
		LOG(("Spy Error: cant write '%1'.").arg(FilePath()));
		return;
	}
	file.write(QJsonDocument(object).toJson(QJsonDocument::Indented));
	if (!file.commit()) {
		LOG(("Spy Error: cant commit '%1'.").arg(FilePath()));
	}
}

rpl::producer<Settings> Changes() {
	return rpl::merge(GlobalChanges.events() | rpl::to_empty,
		Disguise::Changes()) | rpl::map([] { return Current(); });
}

rpl::producer<Settings> Value() {
	return rpl::single(Current()) | rpl::then(Changes());
}

bool PreviewSelfDestructMedia(const HistoryItem *item) {
	return Current().previewSelfDestructMedia
		&& item
		&& !item->out()
		&& item->hasUnreadMediaFlag()
		&& item->media()
		&& item->media()->ttlSeconds() > 0;
}

} // namespace Fork::Spy
