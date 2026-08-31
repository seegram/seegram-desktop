/*
This file is part of SeeGram Desktop,
a Telegram Desktop fork.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "fork/ghost_mode.h"

#include "core/application.h"
#include "core/core_settings.h"
#include "base/unixtime.h"

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
	GlobalSettings.useScheduledMessages = read("useScheduledMessages", false);
}

void Set(const Settings &settings) {
	GlobalSettings = settings;

	auto object = QJsonObject();
	object.insert(u"blockReadReceipts"_q, settings.blockReadReceipts);
	object.insert(u"blockTyping"_q, settings.blockTyping);
	object.insert(u"blockOnlineStatus"_q, settings.blockOnlineStatus);
	object.insert(u"blockUploadProgress"_q, settings.blockUploadProgress);
	object.insert(u"useScheduledMessages"_q, settings.useScheduledMessages);

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

bool UsesScheduledMessages() {
	return GlobalSettings.useScheduledMessages;
}

void ApplyScheduledSend(TimeId &scheduled) {
	if (!GlobalSettings.useScheduledMessages || scheduled) {
		return; // An explicit "send later" is the user's own choice.
	}
	// The date has to still be in the future by the time the server reads it,
	// so a proxy's extra latency gets a wider margin. The 20% figure is
	// AyuGram's, and it is the kind of detail one only learns by shipping it.
	constexpr auto kDelaySeconds = 2;
	const auto proxied = Core::App().settings().proxy().isEnabled();
	const auto delay = proxied
		? ((kDelaySeconds * 6 + 4) / 5)
		: kDelaySeconds;
	scheduled = base::unixtime::now() + delay;
}

} // namespace Fork::Ghost
