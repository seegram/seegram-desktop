#include "fork/seetg/seetg_badge_icons.h"
#include "fork/seetg/seetg_badge_svg.h"
#include "fork/seetg/seetg_http.h"
#include "fork/seetg/seetg_settings.h"
#include "core/application.h"
#include <rpl/event_stream.h>
#include <QtCore/QDir>
#include <QtCore/QDateTime>
#include <QtCore/QFile>
#include <QtCore/QJsonDocument>
#include <QtCore/QJsonObject>
#include <QtCore/QSaveFile>
#include <QtCore/QTimer>

namespace Fork::SeeTg::BadgeIcons {
namespace {
constexpr auto RefreshInterval = crl::time(5 * 60 * 1000);
struct Icon {
 QByteArray svg;
 QByteArray etag;
 crl::time attempted = 0;
 crl::time used = 0;
 bool loaded = false;
 bool loading = false;
};
base::flat_map<QString, Icon> Icons;
rpl::event_stream<> Changed;
QString CacheDir() { return cWorkingDir() + u"tdata/fork_seetg_badges/"_q; }
QString CachePath(const QString &type) { return CacheDir() + type + u".json"_q; }
void Fetch(const QString &type) {
 auto &icon = Icons[type];
 const auto now = crl::now();
 if (!Enabled() || icon.loading || (icon.attempted && now - icon.attempted < RefreshInterval)) return;
 icon.attempted = now;
 icon.loading = true;
 auto headers = std::vector<Http::Header>();
 if (!icon.etag.isEmpty()) headers.push_back({ "If-None-Match", icon.etag });
 Http::Get(u"https://see.tg/desktop/badges/"_q + type + u".svg?v="_q
  + QString::number(QDateTime::currentMSecsSinceEpoch() / RefreshInterval), headers, 15000,
  [=](Http::Response response) {
   auto &icon = Icons[type];
   icon.loading = false;
   if (response.status != 200 || !ValidSvg(response.body)) return;
   const auto changed = icon.svg != response.body;
   icon.svg = std::move(response.body);
   icon.etag = response.etag.left(256);
   QDir().mkpath(CacheDir());
   auto file = QSaveFile(CachePath(type));
   if (file.open(QIODevice::WriteOnly)) {
    const auto json = QJsonDocument(QJsonObject{
     { u"svg"_q, QString::fromLatin1(icon.svg.toBase64()) },
     { u"etag"_q, QString::fromLatin1(icon.etag) },
    }).toJson(QJsonDocument::Compact);
    if (file.write(json) == json.size()) file.commit();
   }
   if (changed) Changed.fire({});
  });
}
void StartTimer() {
 static const auto timer = [] {
  const auto result = new QTimer(QCoreApplication::instance());
  result->setInterval(int(RefreshInterval));
  QObject::connect(result, &QTimer::timeout, [] {
   auto types = QStringList();
   for (const auto &[type, icon] : Icons) {
    types.push_back(type);
   }
   for (const auto &type : types) Fetch(type);
  });
  result->start();
  return result;
 }();
 (void)timer;
}
} // namespace
void Request(const QString &type) {
 if (!ValidType(type) || !Enabled() || (!Icons.contains(type) && Icons.size() >= 256)) return;
 StartTimer();
 auto &icon = Icons[type];
 icon.used = crl::now();
 if (!icon.loaded) {
  icon.loaded = true;
  auto file = QFile(CachePath(type));
  if (file.open(QIODevice::ReadOnly) && file.size() <= 2 * MaxSvgBytes) {
   const auto json = QJsonDocument::fromJson(file.readAll()).object();
   const auto svg = QByteArray::fromBase64(json.value(u"svg"_q).toString().toLatin1());
   if (ValidSvg(svg)) {
    icon.svg = svg;
    icon.etag = json.value(u"etag"_q).toString().toLatin1().left(256);
   }
  }
 }
 Fetch(type);
}
QByteArray Cached(const QString &type) {
 const auto i = Icons.find(type);
 return i == Icons.end() ? QByteArray() : i->second.svg;
}
rpl::producer<> Changes() { return Changed.events(); }
} // namespace Fork::SeeTg::BadgeIcons
