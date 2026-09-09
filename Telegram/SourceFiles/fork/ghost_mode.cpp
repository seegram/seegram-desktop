/*
This file is part of SeeGram Desktop,
a Telegram Desktop fork.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "fork/ghost_mode.h"
#include "fork/disguise.h"

#include "core/application.h"
#include "core/core_settings.h"
#include "api/api_common.h"
#include "base/unixtime.h"

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
	static const auto clean = Settings{ .muteScheduledNotifications = false };
	return Disguise::Clean() ? clean : GlobalSettings;
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
	GlobalSettings.blockStoryViews = read("blockStoryViews", (GlobalSettings.blockReadReceipts && GlobalSettings.blockTyping
			&& GlobalSettings.blockOnlineStatus && GlobalSettings.blockUploadProgress));
	GlobalSettings.useScheduledMessages = read("useScheduledMessages", false);
	GlobalSettings.muteScheduledNotifications = read("muteScheduledNotifications", true);
}

void Set(const Settings &settings) {
	if (Disguise::Clean() || GlobalSettings == settings) {
		return;
	}
	GlobalSettings = settings;
	GlobalChanges.fire_copy(settings);

	auto object = QJsonObject();
	object.insert(u"blockReadReceipts"_q, settings.blockReadReceipts);
	object.insert(u"blockTyping"_q, settings.blockTyping);
	object.insert(u"blockOnlineStatus"_q, settings.blockOnlineStatus);
	object.insert(u"blockUploadProgress"_q, settings.blockUploadProgress);
	object.insert(u"blockStoryViews"_q, settings.blockStoryViews);
	object.insert(u"useScheduledMessages"_q, settings.useScheduledMessages);
	object.insert(u"muteScheduledNotifications"_q, settings.muteScheduledNotifications);

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
	return rpl::merge(GlobalChanges.events() | rpl::to_empty,
		Disguise::Changes()) | rpl::map([] { return Current(); });
}

rpl::producer<Settings> Value() {
	return rpl::single(Current()) | rpl::then(Changes());
}

bool Enabled() {
	return Current().allEnabled();
}

void SetEnabled(bool enabled) {
	auto settings = Current();
	settings.setEnabled(enabled);
	Set(settings);
}

bool BlocksReadReceipts() {
	return Current().blockReadReceipts;
}

bool BlocksTyping() {
	return Current().blockTyping;
}

bool BlocksOnlineStatus() {
	return Current().blockOnlineStatus;
}

bool BlocksUploadProgress() {
	return Current().blockUploadProgress;
}

bool BlocksStoryViews() {
	return Current().blockStoryViews;
}

void RefreshScheduling(Api::SendOptions &options) {
	if (Disguise::Clean() && options.ghostScheduled) {
		options.ghostScheduled = false;
		options.scheduled = 0;
	}
	if (options.ghostScheduled) {
		const auto delay = Core::App().settings().proxy().isEnabled() ? 15 : 12;
		options.scheduled = base::unixtime::now() + delay;
	}
}

void ApplyScheduling(Api::SendAction &action) {
	auto &options = action.options;
	if (!Current().useScheduledMessages
		|| options.scheduled
		|| options.shortcutId
		|| options.welcomeTemplate
		|| options.ttlSeconds
		|| options.suggest.exists
		|| action.replaceMediaOf) {
		return;
	}
	options.ghostScheduled = true;
	RefreshScheduling(options);
}

} // namespace Fork::Ghost
