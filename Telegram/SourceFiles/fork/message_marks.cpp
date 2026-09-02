/*
This file is part of SeeGram Desktop,
a Telegram Desktop fork.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "fork/message_marks.h"

#include "core/application.h"
#include "lang/lang_keys.h"

#include <rpl/event_stream.h>

#include <QtCore/QFile>
#include <QtCore/QJsonDocument>
#include <QtCore/QJsonObject>
#include <QtCore/QSaveFile>

namespace Fork::Marks {
namespace {

constexpr auto kFileName = "tdata/fork_marks.json";

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
		return;
	}
	const auto document = QJsonDocument::fromJson(file.readAll());
	if (!document.isObject()) {
		LOG(("Marks Error: '%1' is not a JSON object, ignoring it."
			).arg(FilePath()));
		return;
	}
	const auto object = document.object();
	const auto readString = [&](const char *key) {
		const auto value = object.value(QLatin1String(key));
		return value.isString() ? value.toString() : QString();
	};
	const auto readBool = [&](const char *key, bool fallback) {
		const auto value = object.value(QLatin1String(key));
		return value.isBool() ? value.toBool() : fallback;
	};
	GlobalSettings.deletedMark = readString("deletedMark");
	GlobalSettings.editedMark = readString("editedMark");
	GlobalSettings.translucentDeleted = readBool(
		"translucentDeleted",
		Settings().translucentDeleted);
}

void Set(const Settings &settings) {
	if (GlobalSettings == settings) {
		return;
	}
	GlobalSettings = settings;
	GlobalChanges.fire_copy(settings);

	auto object = QJsonObject();
	object.insert(u"deletedMark"_q, settings.deletedMark);
	object.insert(u"editedMark"_q, settings.editedMark);
	object.insert(u"translucentDeleted"_q, settings.translucentDeleted);

	auto file = QSaveFile(FilePath());
	if (!file.open(QIODevice::WriteOnly)) {
		LOG(("Marks Error: cant write '%1'.").arg(FilePath()));
		return;
	}
	file.write(QJsonDocument(object).toJson(QJsonDocument::Indented));
	if (!file.commit()) {
		LOG(("Marks Error: cant commit '%1'.").arg(FilePath()));
	}
}

rpl::producer<Settings> Changes() {
	return GlobalChanges.events();
}

rpl::producer<Settings> Value() {
	return rpl::single(GlobalSettings) | rpl::then(Changes());
}

QString DeletedMark() {
	const auto &mark = GlobalSettings.deletedMark;
	return mark.isEmpty() ? DefaultDeletedMark() : mark;
}

QString EditedMark() {
	const auto &mark = GlobalSettings.editedMark;
	return mark.isEmpty() ? DefaultEditedMark() : mark;
}

QString DefaultDeletedMark() {
	return QString::fromUtf8("\xf0\x9f\xa7\xb9"); // Broom.
}

QString DefaultEditedMark() {
	return tr::lng_edited(tr::now);
}

bool TranslucentDeleted() {
	return GlobalSettings.translucentDeleted;
}

} // namespace Fork::Marks
