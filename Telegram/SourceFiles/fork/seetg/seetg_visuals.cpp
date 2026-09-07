/*
This file is part of SeeGram Desktop,
a Telegram Desktop fork.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "fork/seetg/seetg_visuals.h"

#include "fork/seetg/seetg_http.h"
#include "core/application.h"

#include <QtCore/QCryptographicHash>
#include <QtCore/QDir>
#include <QtCore/QFile>
#include <QtCore/QJsonArray>
#include <QtCore/QJsonDocument>
#include <QtCore/QJsonObject>
#include <QtCore/QUrl>

namespace Fork::SeeTg::Visuals {
namespace {

constexpr auto kBackdropsUrl = "https://cdn.changes.tg/gifts/backdrops.json";
constexpr auto kModelUrl = "https://api.changes.tg/model/%1/%2.png?size=128";
constexpr auto kOriginalUrl = "https://api.changes.tg/original/%1.png?size=128";
constexpr auto kUserpicUrl = "https://t.me/i/userpic/160/%1.jpg";
constexpr auto kPatternUrl = "https://api.changes.tg/symbol/%1/%2.png?size=128";
constexpr auto kOriginalAnimationUrl = "https://cdn.changes.tg/gifts/originals/%1/Original.tgs";
constexpr auto kCollectionsUrl = "https://cdn.changes.tg/gifts/id-to-name.json";
constexpr auto kModelsUrl = "https://api.changes.tg/models/%1";
constexpr auto kPatternsUrl = "https://cdn.changes.tg/gifts/patterns.json";
constexpr auto kTimeout = 15 * crl::time(1000);

base::flat_map<QString, Backdrop> BackdropsByName;
bool BackdropsLoaded = false;
bool BackdropsLoading = false;
std::vector<Fn<void()>> BackdropsWaiting;

base::flat_map<QString, QImage> Images;
base::flat_map<QString, std::vector<Fn<void(QImage)>>> ImagesLoading;

[[nodiscard]] QString CacheDir() {
	return cWorkingDir() + u"tdata/fork_seetg_cache/"_q;
}

[[nodiscard]] QString CachePath(const QString &url) {
	const auto hash = QCryptographicHash::hash(
		url.toUtf8(),
		QCryptographicHash::Sha256).toHex().left(32);
	return CacheDir() + QString::fromLatin1(hash);
}

[[nodiscard]] QColor ColorFrom(const QJsonValue &value) {
	return QColor::fromRgb(uint(value.toDouble()));
}

std::vector<std::pair<QString, Backdrop>> BackdropsList;

void ApplyBackdrops(const QByteArray &json) {
	const auto array = QJsonDocument::fromJson(json).array();
	if (array.isEmpty()) {
		return;
	}
	BackdropsByName.clear();
	BackdropsList.clear();
	for (const auto &item : array) {
		const auto object = item.toObject();
		const auto name = object.value(u"name"_q).toString();
		const auto hex = object.value(u"hex"_q).toObject();
		const auto backdrop = Backdrop{
			.center = QColor(hex.value(u"centerColor"_q).toString()),
			.edge = QColor(hex.value(u"edgeColor"_q).toString()),
			.pattern = QColor(hex.value(u"patternColor"_q).toString()),
			.text = QColor(hex.value(u"textColor"_q).toString()),
		};
		if (!name.isEmpty()) {
			BackdropsByName[name.toLower()] = backdrop;
			BackdropsList.push_back({ name, backdrop });
		}
	}
	BackdropsLoaded = !BackdropsByName.empty();
}

void FinishBackdrops() {
	BackdropsLoaded = true;
	BackdropsLoading = false;
	for (const auto &done : base::take(BackdropsWaiting)) {
		done();
	}
}

} // namespace

void Backdrops(Fn<void()> done) {
	if (BackdropsLoaded) {
		done();
		return;
	}
	BackdropsWaiting.push_back(std::move(done));
	if (BackdropsLoading) {
		return;
	}
	BackdropsLoading = true;

	const auto url = QString::fromLatin1(kBackdropsUrl);
	auto cached = QFile(CachePath(url));
	if (cached.open(QIODevice::ReadOnly)) {
		ApplyBackdrops(cached.readAll());
	}
	Http::Get(url, kTimeout, [=](Http::Response response) {
		if (response.status == 200 && !response.body.isEmpty()) {
			ApplyBackdrops(response.body);
			QDir().mkpath(CacheDir());
			auto file = QFile(CachePath(url));
			if (file.open(QIODevice::WriteOnly)) {
				file.write(response.body);
			}
		}
		FinishBackdrops();
	});
}

std::optional<Backdrop> BackdropByName(const QString &name) {
	const auto i = BackdropsByName.find(name.toLower());
	return (i != end(BackdropsByName))
		? std::make_optional(i->second)
		: std::nullopt;
}

QString ModelImageUrl(uint64 giftId, const QString &model) {
	return QString::fromLatin1(kModelUrl)
		.arg(giftId)
		.arg(QString::fromLatin1(QUrl::toPercentEncoding(model)));
}

QString OriginalImageUrl(uint64 giftId) {
	return QString::fromLatin1(kOriginalUrl).arg(giftId);
}

std::vector<std::pair<QString, Backdrop>> AllBackdrops() {
	return BackdropsList;
}

namespace {

base::flat_map<QString, QStringList> PatternsByCollection;
bool PatternsLoaded = false;

} // namespace

void Collections(Fn<void(const std::vector<Collection>&)> done) {
	Fetch(QString::fromLatin1(kCollectionsUrl), [=](QByteArray bytes) {
		auto result = std::vector<Collection>();
		const auto object = QJsonDocument::fromJson(bytes).object();
		for (auto i = object.begin(); i != object.end(); ++i) {
			const auto id = i.key().toULongLong();
			const auto name = i.value().toString();
			if (id && !name.isEmpty()) {
				result.push_back({ id, name });
			}
		}
		ranges::sort(result, std::less<>(), &Collection::name);
		done(result);
	});
}

void Models(uint64 giftId, Fn<void(QStringList)> done) {
	Fetch(QString::fromLatin1(kModelsUrl).arg(giftId), [=](QByteArray bytes) {
		auto result = QStringList();
		for (const auto &item : QJsonDocument::fromJson(bytes).array()) {
			const auto name = item.toObject().value(u"name"_q).toString();
			if (!name.isEmpty()) {
				result.push_back(name);
			}
		}
		result.sort(Qt::CaseInsensitive);
		done(result);
	});
}

void Patterns(Fn<void()> done) {
	if (PatternsLoaded) {
		done();
		return;
	}
	Fetch(QString::fromLatin1(kPatternsUrl), [=](QByteArray bytes) {
		const auto object = QJsonDocument::fromJson(bytes).object();
		for (auto i = object.begin(); i != object.end(); ++i) {
			// "<Collection>/<Pattern>.tgs"
			const auto path = i.value().toString();
			const auto slash = path.indexOf('/');
			if (slash <= 0 || !path.endsWith(u".tgs"_q)) {
				continue;
			}
			const auto collection = path.left(slash);
			const auto pattern = path.mid(slash + 1, path.size() - slash - 5);
			auto &list = PatternsByCollection[collection];
			if (!list.contains(pattern)) {
				list.push_back(pattern);
			}
		}
		PatternsLoaded = !PatternsByCollection.empty();
		done();
	});
}

QStringList PatternNames(const QStringList &collections) {
	auto result = QStringList();
	const auto add = [&](const QStringList &list) {
		for (const auto &name : list) {
			if (!result.contains(name)) {
				result.push_back(name);
			}
		}
	};
	if (collections.isEmpty()) {
		for (const auto &[collection, list] : PatternsByCollection) {
			add(list);
		}
	} else {
		for (const auto &collection : collections) {
			const auto i = PatternsByCollection.find(collection);
			if (i != end(PatternsByCollection)) {
				add(i->second);
			}
		}
	}
	result.sort(Qt::CaseInsensitive);
	return result;
}

QString OriginalAnimationUrl(uint64 giftId) {
	return QString::fromLatin1(kOriginalAnimationUrl).arg(giftId);
}

namespace {

base::flat_map<QString, std::vector<Fn<void(QByteArray)>>> FetchWaiting;

} // namespace

void Fetch(const QString &url, Fn<void(QByteArray)> done) {
	auto cached = QFile(CachePath(url));
	if (cached.open(QIODevice::ReadOnly)) {
		done(cached.readAll());
		return;
	}
	auto &waiting = FetchWaiting[url];
	waiting.push_back(std::move(done));
	if (waiting.size() > 1) {
		return;
	}
	Http::Get(url, kTimeout, [=](Http::Response response) {
		auto bytes = QByteArray();
		if (response.status == 200 && !response.body.isEmpty()) {
			bytes = response.body;
			QDir().mkpath(CacheDir());
			auto file = QFile(CachePath(url));
			if (file.open(QIODevice::WriteOnly)) {
				file.write(bytes);
			}
		}
		const auto i = FetchWaiting.find(url);
		if (i == end(FetchWaiting)) {
			return;
		}
		auto list = std::move(i->second);
		FetchWaiting.erase(i);
		for (const auto &callback : list) {
			callback(bytes);
		}
	});
}

QString PatternUrl(const QString &title, const QString &pattern) {
	return QString::fromLatin1(kPatternUrl)
		.arg(QString::fromLatin1(QUrl::toPercentEncoding(title)))
		.arg(QString::fromLatin1(QUrl::toPercentEncoding(pattern)));
}

QString UserpicUrl(const QString &username) {
	return QString::fromLatin1(kUserpicUrl).arg(username);
}

void Image(const QString &url, Fn<void(QImage)> done) {
	if (const auto i = Images.find(url); i != end(Images)) {
		done(i->second);
		return;
	}
	auto cached = QFile(CachePath(url));
	if (cached.open(QIODevice::ReadOnly)) {
		auto image = QImage::fromData(cached.readAll());
		if (!image.isNull()) {
			Images.emplace(url, image);
			done(std::move(image));
			return;
		}
	}
	const auto loading = ImagesLoading.contains(url);
	ImagesLoading[url].push_back(std::move(done));
	if (loading) {
		return;
	}
	Http::Get(url, kTimeout, [=](Http::Response response) {
		auto image = QImage();
		if (response.status == 200 && !response.body.isEmpty()) {
			image = QImage::fromData(response.body);
			if (!image.isNull()) {
				Images.emplace(url, image);
				QDir().mkpath(CacheDir());
				auto file = QFile(CachePath(url));
				if (file.open(QIODevice::WriteOnly)) {
					file.write(response.body);
				}
			}
		}
		const auto i = ImagesLoading.find(url);
		if (i == end(ImagesLoading)) {
			return;
		}
		auto waiting = std::move(i->second);
		ImagesLoading.erase(i);
		for (const auto &done : waiting) {
			done(image);
		}
	});
}

} // namespace Fork::SeeTg::Visuals
