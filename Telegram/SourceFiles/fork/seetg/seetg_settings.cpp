/*
This file is part of SeeGram Desktop,
a Telegram Desktop fork.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "fork/seetg/seetg_settings.h"
#include "fork/disguise.h"

#include "core/application.h"

#include <rpl/event_stream.h>

#include <QtCore/QFile>
#include <QtCore/QJsonDocument>
#include <QtCore/QJsonObject>
#include <QtCore/QSaveFile>

namespace Fork::SeeTg {
namespace {

constexpr auto kFileName = "tdata/fork_seetg.json";

Settings GlobalSettings;
rpl::event_stream<Settings> GlobalChanges;

[[nodiscard]] QString FilePath() {
	return cWorkingDir() + QString::fromLatin1(kFileName);
}

} // namespace

const Settings &Current() {
	static const auto clean = Settings{ .enabled = false, .usernameFallback = false, .resolveAutomatically = false };
	return Disguise::Clean() ? clean : GlobalSettings;
}

void Start() {
	auto file = QFile(FilePath());
	if (!file.open(QIODevice::ReadOnly)) {
		return;
	}
	const auto document = QJsonDocument::fromJson(file.readAll());
	if (!document.isObject()) {
		LOG(("SeeTg Error: '%1' is not a JSON object, ignoring it."
			).arg(FilePath()));
		return;
	}
	const auto object = document.object();
	const auto defaults = Settings();
	const auto enabled = object.value(u"enabled"_q);
	GlobalSettings.enabled = enabled.isBool()
		? enabled.toBool()
		: defaults.enabled;
	GlobalSettings.gifts = object.value(u"gifts"_q).toBool(true);
	GlobalSettings.transfers = object.value(u"transfers"_q).toBool(true);
	GlobalSettings.comments = object.value(u"comments"_q).toBool(true);
	GlobalSettings.marketPreviews = object.value(u"marketPreviews"_q).toBool(true);
	GlobalSettings.giftDetails = object.value(u"giftDetails"_q).toBool(true);
	GlobalSettings.reactions = object.value(u"reactions"_q).toBool(true);
	GlobalSettings.resolve = (object.value(u"resolve"_q).toString()
		== u"username"_q)
		? ResolveMode::ByUsername
		: ResolveMode::ByGift;
	const auto fallback = object.value(u"usernameFallback"_q);
	GlobalSettings.usernameFallback = fallback.isBool()
		? fallback.toBool()
		: defaults.usernameFallback;
	const auto various = object.value(u"resolveAutomatically"_q);
	GlobalSettings.resolveAutomatically = various.isBool()
		? various.toBool()
		: defaults.resolveAutomatically;
}

void Set(const Settings &settings) {
	if (Disguise::Clean() || GlobalSettings == settings) {
		return;
	}
	GlobalSettings = settings;
	GlobalChanges.fire_copy(settings);

	auto object = QJsonObject();
	object.insert(u"enabled"_q, settings.enabled);
	object.insert(u"gifts"_q, settings.gifts);
	object.insert(u"transfers"_q, settings.transfers);
	object.insert(u"comments"_q, settings.comments);
	object.insert(u"reactions"_q, settings.reactions);
	object.insert(u"giftDetails"_q, settings.giftDetails);
	object.insert(u"marketPreviews"_q, settings.marketPreviews);

	object.insert(
		u"resolve"_q,
		(settings.resolve == ResolveMode::ByUsername)
			? u"username"_q
			: u"gift"_q);
	object.insert(u"usernameFallback"_q, settings.usernameFallback);
	object.insert(u"resolveAutomatically"_q, settings.resolveAutomatically);

	auto file = QSaveFile(FilePath());
	if (!file.open(QIODevice::WriteOnly)) {
		LOG(("SeeTg Error: cant write '%1'.").arg(FilePath()));
		return;
	}
	file.write(QJsonDocument(object).toJson(QJsonDocument::Indented));
	if (!file.commit()) {
		LOG(("SeeTg Error: cant commit '%1'.").arg(FilePath()));
	}
}

rpl::producer<Settings> Changes() {
	return rpl::merge(GlobalChanges.events() | rpl::to_empty,
		Disguise::Changes()) | rpl::map([] { return Current(); });
}

rpl::producer<Settings> Value() {
	return rpl::single(Current()) | rpl::then(Changes());
}

bool Enabled(Feature feature) {
	return Current().featureEnabled(feature);
}

rpl::producer<bool> EnabledValue(Feature feature) {
	return Value() | rpl::map([=](const Settings &settings) {
		return settings.featureEnabled(feature);
	}) | rpl::distinct_until_changed();
}

bool Enabled() {
	return Current().enabled;
}

rpl::producer<bool> EnabledValue() {
	return Value() | rpl::map([](const Settings &settings) {
		return settings.enabled;
	});
}

} // namespace Fork::SeeTg
