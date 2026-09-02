/*
This file is part of SeeGram Desktop,
a Telegram Desktop fork.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "fork/spy_mode.h"

#include "core/application.h"

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
	return GlobalSettings;
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
}

void Set(const Settings &settings) {
	if (GlobalSettings == settings) {
		return;
	}
	GlobalSettings = settings;
	GlobalChanges.fire_copy(settings);

	auto object = QJsonObject();
	object.insert(u"saveDeletedMessages"_q, settings.saveDeletedMessages);
	object.insert(u"saveEditsHistory"_q, settings.saveEditsHistory);
	object.insert(u"saveForBots"_q, settings.saveForBots);

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
	return GlobalChanges.events();
}

rpl::producer<Settings> Value() {
	return rpl::single(GlobalSettings) | rpl::then(Changes());
}

} // namespace Fork::Spy
