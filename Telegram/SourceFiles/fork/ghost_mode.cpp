/*
This file is part of SeeGram Desktop,
a Telegram Desktop fork.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "fork/ghost_mode.h"

#include "core/application.h"

#include <rpl/event_stream.h>

#include <QtCore/QFile>
#include <QtCore/QJsonDocument>
#include <QtCore/QJsonObject>
#include <QtCore/QSaveFile>

namespace Fork::Ghost {
namespace {

// Its own file, in its own format. Core::Settings is deliberately left alone:
// it serializes by hand, field after field, so a field added here would
// collide with every upstream release that adds one there.
constexpr auto kFileName = "tdata/fork_ghost.json";

Settings GlobalSettings;
rpl::event_stream<Settings> GlobalChanges;

[[nodiscard]] QString FilePath() {
	return cWorkingDir() + QString::fromLatin1(kFileName);
}

} // namespace

const Settings &Current() {
	return GlobalSettings;
}

void Start() {
	auto file = QFile(FilePath());
	if (!file.open(QIODevice::ReadOnly)) {
		return; // No file yet: every switch stays off, which is upstream.
	}
	const auto document = QJsonDocument::fromJson(file.readAll());
	if (!document.isObject()) {
		LOG(("Ghost Error: '%1' is not a JSON object, ignoring it."
			).arg(FilePath()));
		return;
	}
	const auto object = document.object();
	const auto read = [&](const char *key, bool fallback) {
		const auto value = object.value(QLatin1String(key));
		return value.isBool() ? value.toBool() : fallback;
	};
	GlobalSettings.blockReadReceipts = read("blockReadReceipts", false);
	GlobalSettings.blockTyping = read("blockTyping", false);
	GlobalSettings.blockOnlineStatus = read("blockOnlineStatus", false);
	GlobalSettings.blockUploadProgress = read("blockUploadProgress", false);
}

void Set(const Settings &settings) {
	if (GlobalSettings == settings) {
		return;
	}
	GlobalSettings = settings;
	GlobalChanges.fire_copy(settings);

	auto object = QJsonObject();
	object.insert(u"blockReadReceipts"_q, settings.blockReadReceipts);
	object.insert(u"blockTyping"_q, settings.blockTyping);
	object.insert(u"blockOnlineStatus"_q, settings.blockOnlineStatus);
	object.insert(u"blockUploadProgress"_q, settings.blockUploadProgress);

	// QSaveFile so that a crash mid-write cannot leave a truncated file, which
	// would read back as "ghost mode off" - the failure nobody would notice.
	auto file = QSaveFile(FilePath());
	if (!file.open(QIODevice::WriteOnly)) {
		LOG(("Ghost Error: cant write '%1'.").arg(FilePath()));
		return;
	}
	file.write(QJsonDocument(object).toJson(QJsonDocument::Indented));
	if (!file.commit()) {
		LOG(("Ghost Error: cant commit '%1'.").arg(FilePath()));
	}
}

rpl::producer<Settings> Changes() {
	return GlobalChanges.events();
}

rpl::producer<Settings> Value() {
	return rpl::single(GlobalSettings) | rpl::then(Changes());
}

bool Enabled() {
	return GlobalSettings.allEnabled();
}

void SetEnabled(bool enabled) {
	Set({
		.blockReadReceipts = enabled,
		.blockTyping = enabled,
		.blockOnlineStatus = enabled,
		.blockUploadProgress = enabled,
	});
}

bool BlocksReadReceipts() {
	return GlobalSettings.blockReadReceipts;
}

bool BlocksTyping() {
	return GlobalSettings.blockTyping;
}

bool BlocksOnlineStatus() {
	return GlobalSettings.blockOnlineStatus;
}

bool BlocksUploadProgress() {
	return GlobalSettings.blockUploadProgress;
}

} // namespace Fork::Ghost
